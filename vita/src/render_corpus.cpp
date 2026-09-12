// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "render_corpus.h"

#include <array>
#include <bit>
#include <cstring>
#include <vector>

#include <nihstro/inline_assembly.h>

#include "common/color.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/vector_math.h"
#include "core/arm/arm_interface.h"
#include "core/arm/dyncom/arm_dyncom.h"
#include "core/arm/dyncom/arm_dyncom_environment.h"
#include "core/core_timing.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/thread.h"
#include "core/loader/3dsx_image.h"
#include "core/memory.h"
#include "core/memory_environment.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/regs_external.h"
#include "video_core/renderer_environment.h"
#include "video_core/renderer_software/renderer_software.h"
#include "video_core/renderer_software/sw_blitter.h"
#include "video_core/utils.h"

namespace Vita::RenderProbe {
namespace {

u32 Mix(u32 signature, u32 value) {
    return (signature ^ value) * 16777619U;
}

// ---------------------------------------------------------------------------------------------
// PICA float24 and raw command list encoding.
//
// Pica::PicaCore has no public "write this register" API besides feeding it a real command list
// through ProcessCmdList() - the exact format real hardware and real homebrew use, parsed at
// pica_core.cpp's ProcessCmdList/WriteInternalReg. This corpus builds that format directly rather
// than reimplementing register semantics some other way, so what it exercises is the real,
// production command dispatch - not a testing shortcut around it.
// ---------------------------------------------------------------------------------------------

/// Encodes a float32 into PICA's float24 (1 sign, 7 exponent, 16 mantissa; bias 63) raw bit
/// pattern, the inverse of Pica::f24::FromRaw. Only exact for values whose low 7 mantissa bits are
/// zero and whose exponent is within float24's narrower range - true for every constant this corpus
/// uses (0, 1, -1, 0.5, 32, ...), so no rounding path is implemented.
constexpr u32 EncodeF24Raw(float v) {
    if (v == 0.0f) {
        return 0;
    }
    const u32 bits = std::bit_cast<u32>(v);
    const u32 sign = bits >> 31;
    const u32 exp32 = (bits >> 23) & 0xFF;
    const u32 mantissa32 = bits & 0x7FFFFF;
    const u32 exp24 = exp32 - 64; // float32 bias 127, float24 bias 63: shift by (127-63)=64
    return (sign << 23) | ((exp24 & 0x7F) << 16) | (mantissa32 >> 7);
}

/// Packs four float24 values into the three 32-bit words PICA's immediate-mode vertex submission
/// (pipeline.vs_default_attributes_setup.set_value[0..2]) expects, in push order - the exact inverse
/// of Pica::PackedAttribute::AsFloat24() (video_core/pica/packed_attribute.h).
constexpr std::array<u32, 3> PackImmediateF24(float x, float y, float z, float w) {
    const u32 xr = EncodeF24Raw(x);
    const u32 yr = EncodeF24Raw(y);
    const u32 zr = EncodeF24Raw(z);
    const u32 wr = EncodeF24Raw(w);
    std::array<u32, 3> buffer{};
    buffer[0] = (wr << 8) | ((zr >> 16) & 0xFF);
    buffer[1] = ((zr & 0xFFFF) << 16) | ((yr >> 8) & 0xFFFF);
    buffer[2] = ((yr & 0xFF) << 24) | (xr & 0xFFFFFF);
    return buffer;
}

/// Builds a raw PICA command list: pairs of (value, header) words, one pair per register write,
/// always with parameter_mask=0xF (write all four bytes) and no batch/sequential extra data - see
/// pica_core.cpp's private CommandHeader union, which this mirrors exactly (cmd_id bits 0-15,
/// parameter_mask bits 16-19, extra_data_length bits 20-27, group_commands bit 31). Every register
/// this corpus needs can be written one word at a time, including the "set_value"/"set_word"
/// registers that accumulate state across repeated single writes (immediate-mode vertices, shader
/// program upload) - see PicaCore::HandleSpecialReg/HandleSpecialRegBatch.
class CommandListBuilder {
public:
    void Write(u32 register_index, u32 value) {
        words.push_back(value);
        words.push_back((register_index & 0xFFFFu) | (0xFu << 16));
    }

    std::vector<u8> Bytes() const {
        std::vector<u8> bytes(words.size() * sizeof(u32));
        std::memcpy(bytes.data(), words.data(), bytes.size());
        return bytes;
    }

private:
    std::vector<u32> words;
};

/// All six TEV stages run unconditionally for every pixel (sw_rasterizer.cpp's WriteTevConfig has
/// no per-stage enable bit) - a zero-initialized stage defaults to Source::PrimaryColor with
/// Operation::Replace, so an unconfigured stage 1 would discard whatever stage 0 computed and
/// substitute the interpolated vertex color again. A real citro3d program's C3D_TexEnvInit sets
/// every stage this way for the same reason. Both command list builders below configure stage 0
/// themselves and call this for stages 1-5, so stage 0's result survives the rest of the chain
/// unchanged.
void WritePassthroughTevChain(CommandListBuilder& cmd) {
    constexpr u32 PreviousBothChannels = (0xFu << 16) | 0xFu; // color_source1=alpha_source1=Previous
    cmd.Write(PICA_REG_INDEX(texturing.tev_stage1.sources_raw), PreviousBothChannels);
    cmd.Write(PICA_REG_INDEX(texturing.tev_stage2.sources_raw), PreviousBothChannels);
    cmd.Write(PICA_REG_INDEX(texturing.tev_stage3.sources_raw), PreviousBothChannels);
    cmd.Write(PICA_REG_INDEX(texturing.tev_stage4.sources_raw), PreviousBothChannels);
    cmd.Write(PICA_REG_INDEX(texturing.tev_stage5.sources_raw), PreviousBothChannels);
};

// ---------------------------------------------------------------------------------------------
// Fixed physical scratch layout.
//
// Every group below (except guest_frame, which loads a real process image at
// Memory::PROCESS_IMAGE_VADDR) pokes these physical FCRAM offsets directly through
// Memory::MemorySystem::GetPhysicalPointer, exactly like the Hito 3/4 corpora already do. They are
// spaced far enough apart that no two groups' scratch data can ever alias.
// ---------------------------------------------------------------------------------------------

constexpr PAddr FormatsSourcePAddr = Memory::FCRAM_PADDR + 0x0000'0000;
constexpr PAddr TransferSrcPAddr = Memory::FCRAM_PADDR + 0x0001'0000;
constexpr PAddr TransferDstPAddr = Memory::FCRAM_PADDR + 0x0002'0000;
constexpr PAddr TriangleTargetPAddr = Memory::FCRAM_PADDR + 0x0003'0000;
constexpr PAddr TexturedTargetPAddr = Memory::FCRAM_PADDR + 0x0004'0000;
constexpr PAddr TexturePAddr = Memory::FCRAM_PADDR + 0x0005'0000;
constexpr PAddr GuestTargetPAddr = Memory::FCRAM_PADDR + 0x0006'0000;
constexpr PAddr GuestListPAddr = Memory::FCRAM_PADDR + 0x0007'0000;

constexpr u32 RenderTargetSize = 64; // 64x64 RGBA8 render targets throughout this corpus.
constexpr u32 RenderTargetBytes = RenderTargetSize * RenderTargetSize * 4;
constexpr u32 TextureSize = 8; // Minimum PICA tile granularity; every texel is filled identically
                               // (see RunTexturedQuadGroup), so this corpus never has to reproduce
                               // the Morton texture tiling scheme to know what to expect back.
constexpr u32 TextureBytes = TextureSize * TextureSize * 4;

/// Minimal Core::MemoryEnvironment + VideoCore::RendererEnvironment: enough to construct a real
/// Memory::MemorySystem, Pica::PicaCore and SwRenderer::RendererSoftware without Core::System or a
/// Frontend::EmuWindow. Mirrors budget_corpus.cpp's PlainEnvironment and system_corpus.cpp's
/// SystemEnvironment; every group below except guest_frame builds one of these.
class ProbeGpuEnvironment final : public Core::MemoryEnvironment,
                                  public VideoCore::RendererEnvironment {
public:
    void SetMemory(Memory::MemorySystem& memory_) {
        memory = &memory_;
    }

    // Core::MemoryEnvironment
    bool IsPoweredOn() const override {
        return true;
    }
    VAddr GetRunningCorePC() const override {
        return 0;
    }
    std::shared_ptr<Kernel::Process> GetCurrentProcess() const override {
        return nullptr;
    }
    void FlushRegion(PAddr, u32) override {}
    void InvalidateRegion(PAddr, u32) override {}
    void FlushAndInvalidateRegion(PAddr, u32) override {}
    u32 ReadIoRegister32(VAddr) override {
        return 0;
    }
    void WriteIoRegister32(VAddr, u32) override {}
    void LogUnmappedAccess(Core::ExceptionType) override {}

    // VideoCore::RendererEnvironment
    Memory::MemorySystem& Memory() override {
        return *memory;
    }

private:
    Memory::MemorySystem* memory{};
};

/// One constructed Memory::MemorySystem + Pica::PicaCore + SwRenderer::RendererSoftware, wired
/// together exactly as VideoCore::GPU::Impl wires the real ones (pica_core.h's ctor list) but
/// without Core::System, VideoCore::GPU, or any HLE service. Used by every group below except
/// guest_frame (which needs the loader/kernel/CPU stack instead - see RunGuestFrameGroup).
/// Keeps signatures comparable between the x86-64 desktop reference and the ARMv7 Vita probe
/// regardless of Settings::values.use_shader_jit's default: the PICA vertex shader JIT is only
/// compiled for x86-64/arm64 (video_core/shader/shader.cpp's CreateEngine), so an ARMv7 build
/// always falls back to the interpreter - forcing it here too keeps both sides on the exact same
/// shader engine rather than leaving that to whatever the desktop host's default happens to be.
bool ForceInterpreterShaderEngine() {
    Settings::values.use_shader_jit.SetValue(false);
    return true;
}

struct GpuStack {
    bool jit_disabled = ForceInterpreterShaderEngine(); // must init before pica, see above
    ProbeGpuEnvironment environment;
    Memory::MemorySystem memory;
    // Members initialize in declaration order regardless of the constructor's mem-initializer-list
    // order, so this - not a statement in the constructor body - is what guarantees
    // environment.Memory() is valid by the time renderer's constructor calls it below. Without it,
    // RendererSoftware binds its Memory::MemorySystem& reference to environment.Memory()'s not-yet-
    // set backing pointer (still null at that point) - undefined behavior, caught as a crash the
    // first time anything dereferences it.
    bool memory_wired = (environment.SetMemory(memory), true);
    Pica::PicaCore pica;
    SwRenderer::RendererSoftware renderer;
    SwRenderer::SwBlitter blitter;

    GpuStack()
        : memory(environment), pica(memory, nullptr), renderer(environment, pica),
          blitter(memory, renderer.Rasterizer()) {
        pica.BindRasterizer(renderer.Rasterizer());
    }
};

u8* Ptr(Memory::MemorySystem& memory, PAddr addr) {
    return memory.GetPhysicalPointer(addr);
}

void FillRegion(Memory::MemorySystem& memory, PAddr addr, u32 size, u8 value) {
    std::memset(Ptr(memory, addr), value, size);
}

bool RegionEquals(Memory::MemorySystem& memory, PAddr addr, u32 size, u8 value) {
    const u8* p = Ptr(memory, addr);
    for (u32 i = 0; i < size; ++i) {
        if (p[i] != value) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Flat-shaded triangle command list.
//
// Draws two triangles (a full-viewport quad) in PICA immediate mode with a single flat vertex
// color, sampling no textures and relying on the TEV combiner's power-on default (stage 0: Replace,
// source PrimaryColor - see regs_texturing.h's TevStageConfig, zero-initialized and never written
// here) to pass the interpolated vertex color straight through. Every vertex shares the same color,
// so the render target is expected to end up as that flat color everywhere, regardless of exactly
// how triangle scan conversion or Morton tiling place individual pixels - see the group functions'
// verification, which checks every byte instead of computing expected per-pixel offsets by hand.
// ---------------------------------------------------------------------------------------------

std::vector<u8> BuildFlatQuadCommandList(PAddr render_target_addr, Common::Vec4<u8> color) {
    CommandListBuilder cmd;

    // Vertex shader: passes attribute 0 (position) to o0 and attribute 1 (color) to o1 unchanged.
    // Compiled the same way src/tests/video_core/shader.cpp compiles its test shaders.
    // clang-format off
    const auto shbin = nihstro::InlineAsm::CompileToRawBinary({
        {nihstro::OpCode::Id::MOV, nihstro::DestRegister::MakeOutput(0), "xyzw",
         nihstro::SourceRegister::MakeInput(0), "xyzw", nihstro::SourceRegister{}, ""},
        {nihstro::OpCode::Id::MOV, nihstro::DestRegister::MakeOutput(1), "xyzw",
         nihstro::SourceRegister::MakeInput(1), "xyzw", nihstro::SourceRegister{}, ""},
        {nihstro::OpCode::Id::END},
    });
    // clang-format on

    for (const auto& word : shbin.program) {
        cmd.Write(PICA_REG_INDEX(vs.program.set_word[0]), word.hex);
    }
    for (const auto& word : shbin.swizzle_table) {
        cmd.Write(PICA_REG_INDEX(vs.swizzle_patterns.set_word[0]), word.hex);
    }
    cmd.Write(PICA_REG_INDEX(vs.main_offset), 0);
    // input attribute 0 -> register v0, attribute 1 -> register v1 (identity mapping).
    cmd.Write(PICA_REG_INDEX(vs.input_attribute_to_register_map_low), 0x10);
    cmd.Write(PICA_REG_INDEX(vs.input_buffer_config), 1 | (0xA0u << 24)); // 2 attributes, mode VS
    cmd.Write(PICA_REG_INDEX(vs.output_mask), 0b11); // o0, o1

    // rasterizer: attribute 0 -> POSITION_X/Y/Z/W, attribute 1 -> COLOR_R/G/B/A.
    cmd.Write(PICA_REG_INDEX(rasterizer.vs_output_total), 2);
    cmd.Write(PICA_REG_INDEX(rasterizer.vs_output_attributes[0]), 0x03020100);
    cmd.Write(PICA_REG_INDEX(rasterizer.vs_output_attributes[1]), 0x0B0A0908);
    cmd.Write(PICA_REG_INDEX(rasterizer.viewport_corner), 0);
    cmd.Write(PICA_REG_INDEX(rasterizer.viewport_size_x),
             EncodeF24Raw(static_cast<float>(RenderTargetSize) / 2.0f));
    cmd.Write(PICA_REG_INDEX(rasterizer.viewport_size_y),
             EncodeF24Raw(static_cast<float>(RenderTargetSize) / 2.0f));

    cmd.Write(PICA_REG_INDEX(lighting.disable), 1); // no lights configured; skip that path entirely
    WritePassthroughTevChain(cmd); // stage 0 stays at its default (PrimaryColor, Replace)

    // output_merger: straight alpha blend (One, Zero == plain overwrite), no depth/stencil test,
    // all four color channels enabled. Needed explicitly - the power-on default is logic-op Clear,
    // which would zero every pixel this corpus draws (see sw_framebuffer.cpp's LogicOp).
    cmd.Write(PICA_REG_INDEX(framebuffer.output_merger.alphablend_enable), 1u << 8);
    cmd.Write(PICA_REG_INDEX(framebuffer.output_merger.alpha_blending.blend_equation_rgb),
             (1u << 16) | (1u << 24)); // factor_source_rgb=One, factor_source_a=One; dest=Zero(0)
    cmd.Write(PICA_REG_INDEX(framebuffer.output_merger.depth_color_mask),
             (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11)); // R/G/B/A write enable, depth disabled

    // framebuffer render target: RGBA8, render_target_addr, RenderTargetSize square.
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.allow_color_write), 1);
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.color_format), 0); // RGBA8
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.color_buffer_address), render_target_addr / 8);
    // Depth testing is disabled (output_merger.depth_color_mask above), but SwRenderer::Framebuffer
    // ::Bind() resolves both buffer addresses unconditionally (sw_framebuffer.cpp) - leaving this at
    // its zero default would resolve physical address 0, logging a harmless but noisy "Unknown
    // GetPhysMemRegionInfo" error. Point it just past the color buffer instead; nothing ever reads
    // or writes through it since depth/stencil testing never runs.
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.depth_buffer_address),
             (render_target_addr + RenderTargetBytes) / 8);
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.width),
             RenderTargetSize | ((RenderTargetSize - 1) << 12));

    cmd.Write(PICA_REG_INDEX(pipeline.max_input_attrib_index), 1); // 2 attributes: position, color

    // Enter immediate mode (any write to .index resets and arms it; IMMEDIATE_MODE_INDEX == 0xF).
    cmd.Write(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.index), 0xF);

    const auto push_vertex = [&](float x, float y, Common::Vec4<u8> vertex_color) {
        const auto pos = PackImmediateF24(x, y, 0.0f, 1.0f);
        for (u32 word : pos) {
            cmd.Write(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]), word);
        }
        const auto col = PackImmediateF24(vertex_color.r() / 255.0f, vertex_color.g() / 255.0f,
                                          vertex_color.b() / 255.0f, vertex_color.a() / 255.0f);
        for (u32 word : col) {
            cmd.Write(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]), word);
        }
    };

    // Two triangles covering the full [-1, 1] NDC square, same flat color on every vertex.
    push_vertex(-1.0f, -1.0f, color);
    push_vertex(+1.0f, -1.0f, color);
    push_vertex(-1.0f, +1.0f, color);
    push_vertex(+1.0f, -1.0f, color);
    push_vertex(+1.0f, +1.0f, color);
    push_vertex(-1.0f, +1.0f, color);

    return cmd.Bytes();
}

} // namespace

bool RenderReport::Passed() const {
    if (group_count != groups.size()) {
        return false;
    }
    for (const auto& group : groups) {
        if (!group.passed) {
            return false;
        }
    }
    return true;
}

GroupResult RunRendererInitGroup() {
    GpuStack stack;
    const bool rasterizer_bound = stack.renderer.Rasterizer() != nullptr;
    // InitializeRegs()'s desktop-inherited defaults (nn::gx::Initialize's values) - checked here as
    // proof the trio came up in a known, sane state, not because this corpus depends on them.
    const auto& top = stack.pica.regs.framebuffer_config[0];
    const auto& bottom = stack.pica.regs.framebuffer_config[1];
    const bool defaults_ok = top.width == 240 && top.height == 400 && bottom.width == 240 &&
                             bottom.height == 320;
    stack.renderer.SwapBuffers();
    const auto& top_screen = stack.renderer.Screen(VideoCore::ScreenId::TopLeft);
    const auto& bottom_screen = stack.renderer.Screen(VideoCore::ScreenId::Bottom);
    const bool screens_ok = top_screen.width == 240 && top_screen.height == 400 &&
                            bottom_screen.width == 240 && bottom_screen.height == 320;
    const bool passed = rasterizer_bound && defaults_ok && screens_ok;
    return {"renderer_init", passed, Mix(2166136261U, passed ? 1 : 0)};
}

GroupResult RunColorFillGroup() {
    GpuStack stack;
    constexpr Common::Vec4<u8> TopColor{0x20, 0x40, 0x80, 0xFF};
    constexpr Common::Vec4<u8> BottomColor{0xC0, 0x60, 0x10, 0xFF};

    auto& top_fill = stack.pica.regs_lcd.color_fill_top;
    top_fill.is_enabled.Assign(1);
    top_fill.color_r.Assign(TopColor.r());
    top_fill.color_g.Assign(TopColor.g());
    top_fill.color_b.Assign(TopColor.b());

    auto& bottom_fill = stack.pica.regs_lcd.color_fill_bottom;
    bottom_fill.is_enabled.Assign(1);
    bottom_fill.color_r.Assign(BottomColor.r());
    bottom_fill.color_g.Assign(BottomColor.g());
    bottom_fill.color_b.Assign(BottomColor.b());

    stack.renderer.SwapBuffers();

    const auto& top_screen = stack.renderer.Screen(VideoCore::ScreenId::TopLeft);
    const auto& bottom_screen = stack.renderer.Screen(VideoCore::ScreenId::Bottom);
    const bool top_ok = top_screen.pixels.size() >= 4 && top_screen.pixels[0] == TopColor.r() &&
                        top_screen.pixels[1] == TopColor.g() &&
                        top_screen.pixels[2] == TopColor.b() && top_screen.pixels[3] == 255;
    const bool bottom_ok = bottom_screen.pixels.size() >= 4 &&
                           bottom_screen.pixels[0] == BottomColor.r() &&
                           bottom_screen.pixels[1] == BottomColor.g() &&
                           bottom_screen.pixels[2] == BottomColor.b() &&
                           bottom_screen.pixels[3] == 255;
    const bool passed = top_ok && bottom_ok;
    return {"color_fill", passed,
           Mix(Mix(2166136261U, top_ok ? 1 : 0), bottom_ok ? 1 : 0)};
}

GroupResult RunFramebufferFormatsGroup() {
    GpuStack stack;
    constexpr Common::Vec4<u8> TestColor{0x64, 0x96, 0xC8, 0xFF};

    const std::array<Pica::PixelFormat, 5> formats{
        Pica::PixelFormat::RGBA8, Pica::PixelFormat::RGB8, Pica::PixelFormat::RGB565,
        Pica::PixelFormat::RGB5A1, Pica::PixelFormat::RGBA4,
    };

    // RendererSoftware::LoadFBToScreenInfo reads column x from byte offset (pixel_stride - x) * bpp
    // (renderer_software.cpp), not x itself - a deliberate reversal (the 3DS screen is portrait; see
    // that file), but it means a single isolated test pixel at offset 0 is never actually read for
    // any x. Sidestepping that the same way RunTriangleRasterGroup sidesteps Morton tiling: fill the
    // whole scratch region with the identical encoded pixel repeated, so every offset the decode
    // path can possibly read yields the same expected color.
    constexpr u32 ScratchBytes = 64;
    constexpr u32 TestWidth = 8;

    u32 signature = 2166136261U;
    bool all_ok = true;
    for (const auto format : formats) {
        u8 pixel_bytes[4]{};
        switch (format) {
        case Pica::PixelFormat::RGBA8:
            Common::Color::EncodeRGBA8(TestColor, pixel_bytes);
            break;
        case Pica::PixelFormat::RGB8:
            Common::Color::EncodeRGB8(TestColor, pixel_bytes);
            break;
        case Pica::PixelFormat::RGB565:
            Common::Color::EncodeRGB565(TestColor, pixel_bytes);
            break;
        case Pica::PixelFormat::RGB5A1:
            Common::Color::EncodeRGB5A1(TestColor, pixel_bytes);
            break;
        case Pica::PixelFormat::RGBA4:
            Common::Color::EncodeRGBA4(TestColor, pixel_bytes);
            break;
        }

        const u32 bpp = Pica::BytesPerPixel(format);
        u8* src = Ptr(stack.memory, FormatsSourcePAddr);
        for (u32 offset = 0; offset + bpp <= ScratchBytes; offset += bpp) {
            std::memcpy(src + offset, pixel_bytes, bpp);
        }

        auto& top = stack.pica.regs.framebuffer_config[0];
        top.color_format.Assign(format);
        top.address_left1 = FormatsSourcePAddr;
        top.active_fb = 0;
        top.width.Assign(TestWidth);
        top.height.Assign(1);
        top.stride = TestWidth * bpp;
        stack.pica.regs_lcd.color_fill_top.is_enabled.Assign(0);

        stack.renderer.SwapBuffers();
        const auto& screen = stack.renderer.Screen(VideoCore::ScreenId::TopLeft);

        // RGB5A1/RGB565/RGBA4 lose precision on the way in; compare against the same decode a
        // correct implementation would produce, not the original TestColor bit for bit.
        const Common::Vec4<u8> expected = [&] {
            switch (format) {
            case Pica::PixelFormat::RGBA8:
                return Common::Color::DecodeRGBA8(src);
            case Pica::PixelFormat::RGB8:
                return Common::Color::DecodeRGB8(src);
            case Pica::PixelFormat::RGB565:
                return Common::Color::DecodeRGB565(src);
            case Pica::PixelFormat::RGB5A1:
                return Common::Color::DecodeRGB5A1(src);
            case Pica::PixelFormat::RGBA4:
                return Common::Color::DecodeRGBA4(src);
            default:
                return Common::Vec4<u8>{0, 0, 0, 0};
            }
        }();

        const bool ok = screen.pixels.size() >= 4 && screen.pixels[0] == expected.r() &&
                        screen.pixels[1] == expected.g() && screen.pixels[2] == expected.b() &&
                        screen.pixels[3] == expected.a();
        all_ok = all_ok && ok;
        signature = Mix(signature, ok ? 1 : 0);
    }

    return {"framebuffer_formats", all_ok, signature};
}

GroupResult RunTransferEngineGroup() {
    GpuStack stack;
    constexpr u32 FillBytes = 256;
    constexpr u32 FillValue = 0x44444444; // every byte identical, so RegionEquals can check it

    // MemoryFill: SwRenderer::SwBlitter::MemoryFill directly, mirroring VideoCore::GPU::MemoryFill
    // (see docs/vita-port.md - no VideoCore::GPU or Core::System is linked into this port).
    Pica::MemoryFillConfig fill_config{};
    fill_config.address_start = static_cast<u32>(TransferSrcPAddr) / 8;
    fill_config.address_end = static_cast<u32>(TransferSrcPAddr + FillBytes) / 8;
    fill_config.value_32bit = FillValue;
    fill_config.control = 0;
    fill_config.fill_32bit.Assign(1);
    stack.blitter.MemoryFill(fill_config);
    const bool fill_ok = RegionEquals(stack.memory, TransferSrcPAddr, FillBytes, 0x44) &&
                         *reinterpret_cast<const u32*>(Ptr(stack.memory, TransferSrcPAddr)) ==
                             FillValue;

    // TextureCopy: a raw memcpy between physical addresses (see sw_blitter.cpp), the same transfer
    // mode a real homebrew uses to move a same-format render target into the LCD framebuffer.
    FillRegion(stack.memory, TransferDstPAddr, FillBytes, 0x00);
    Pica::DisplayTransferConfig transfer_config{};
    transfer_config.input_address = static_cast<u32>(TransferSrcPAddr) / 8;
    transfer_config.output_address = static_cast<u32>(TransferDstPAddr) / 8;
    transfer_config.flags = 0;
    transfer_config.is_texture_copy.Assign(1);
    transfer_config.texture_copy.size = FillBytes;
    transfer_config.texture_copy.input_size = 0; // gap=0 -> contiguous, width = whole size
    transfer_config.texture_copy.output_size = 0;
    stack.blitter.TextureCopy(transfer_config);
    const bool copy_ok = RegionEquals(stack.memory, TransferDstPAddr, FillBytes, 0x44);

    const bool passed = fill_ok && copy_ok;
    return {"transfer_engine", passed,
           Mix(Mix(2166136261U, fill_ok ? 1 : 0), copy_ok ? 1 : 0)};
}

GroupResult RunTriangleRasterGroup() {
    GpuStack stack;
    constexpr Common::Vec4<u8> FlatColor{0xFF, 0x00, 0x00, 0xFF}; // pure red: exact through
                                                                  // interpolation and encoding
    FillRegion(stack.memory, TriangleTargetPAddr, RenderTargetBytes, 0xAA); // sentinel

    const auto command_list = BuildFlatQuadCommandList(TriangleTargetPAddr, FlatColor);
    u8* list_dest = Ptr(stack.memory, GuestListPAddr);
    std::memcpy(list_dest, command_list.data(), command_list.size());
    stack.pica.ProcessCmdList(GuestListPAddr, static_cast<u32>(command_list.size()), false);

    bool passed = true;
    for (u32 y = 0; y < RenderTargetSize && passed; ++y) {
        for (u32 x = 0; x < RenderTargetSize && passed; ++x) {
            const u32 offset = VideoCore::GetMortonOffset(x, y, 4) +
                               (y & ~7u) * RenderTargetSize * 4;
            const u8* pixel = Ptr(stack.memory, TriangleTargetPAddr) + offset;
            // Common::Color::EncodeRGBA8/DecodeRGBA8 store bytes in reverse order (byte 0 = alpha,
            // byte 3 = red - see common/color.h), so this decodes back through the same pair the
            // rasterizer's own Framebuffer::DrawPixel used, rather than assuming a byte layout.
            if (Common::Color::DecodeRGBA8(pixel) != FlatColor) {
                passed = false;
            }
        }
    }

    return {"triangle_raster", passed, Mix(2166136261U, passed ? 1 : 0)};
}

namespace {

// A second command list builder, deliberately separate from BuildFlatQuadCommandList rather than
// parameterized over their shared shape: the two differ in which rasterizer output semantics and
// TEV source they use (interpolated vertex color vs. a sampled texture), not just in the values
// plugged into an otherwise-identical template.
std::vector<u8> BuildTexturedQuadCommandList(PAddr render_target_addr, PAddr texture_addr) {
    CommandListBuilder cmd;

    // clang-format off
    const auto shbin = nihstro::InlineAsm::CompileToRawBinary({
        {nihstro::OpCode::Id::MOV, nihstro::DestRegister::MakeOutput(0), "xyzw",
         nihstro::SourceRegister::MakeInput(0), "xyzw", nihstro::SourceRegister{}, ""},
        {nihstro::OpCode::Id::MOV, nihstro::DestRegister::MakeOutput(1), "xyzw",
         nihstro::SourceRegister::MakeInput(1), "xyzw", nihstro::SourceRegister{}, ""},
        {nihstro::OpCode::Id::END},
    });
    // clang-format on

    for (const auto& word : shbin.program) {
        cmd.Write(PICA_REG_INDEX(vs.program.set_word[0]), word.hex);
    }
    for (const auto& word : shbin.swizzle_table) {
        cmd.Write(PICA_REG_INDEX(vs.swizzle_patterns.set_word[0]), word.hex);
    }
    cmd.Write(PICA_REG_INDEX(vs.main_offset), 0);
    cmd.Write(PICA_REG_INDEX(vs.input_attribute_to_register_map_low), 0x10);
    cmd.Write(PICA_REG_INDEX(vs.input_buffer_config), 1 | (0xA0u << 24));
    cmd.Write(PICA_REG_INDEX(vs.output_mask), 0b11);

    // attribute 0 -> position, attribute 1 -> TEXCOORD0 (u,v); z/w of attribute 1 are left
    // unmapped (INVALID), landing in OutputVertex's unused overflow slot - see output_vertex.cpp.
    cmd.Write(PICA_REG_INDEX(rasterizer.vs_output_total), 2);
    cmd.Write(PICA_REG_INDEX(rasterizer.vs_output_attributes[0]), 0x03020100);
    cmd.Write(PICA_REG_INDEX(rasterizer.vs_output_attributes[1]), 0x1F1F0D0C);
    cmd.Write(PICA_REG_INDEX(rasterizer.viewport_corner), 0);
    cmd.Write(PICA_REG_INDEX(rasterizer.viewport_size_x),
             EncodeF24Raw(static_cast<float>(RenderTargetSize) / 2.0f));
    cmd.Write(PICA_REG_INDEX(rasterizer.viewport_size_y),
             EncodeF24Raw(static_cast<float>(RenderTargetSize) / 2.0f));

    cmd.Write(PICA_REG_INDEX(lighting.disable), 1);

    cmd.Write(PICA_REG_INDEX(framebuffer.output_merger.alphablend_enable), 1u << 8);
    cmd.Write(PICA_REG_INDEX(framebuffer.output_merger.alpha_blending.blend_equation_rgb),
             (1u << 16) | (1u << 24));
    cmd.Write(PICA_REG_INDEX(framebuffer.output_merger.depth_color_mask),
             (1u << 8) | (1u << 9) | (1u << 10) | (1u << 11));

    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.allow_color_write), 1);
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.color_format), 0);
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.color_buffer_address), render_target_addr / 8);
    // Depth testing is disabled (output_merger.depth_color_mask above), but SwRenderer::Framebuffer
    // ::Bind() resolves both buffer addresses unconditionally (sw_framebuffer.cpp) - leaving this at
    // its zero default would resolve physical address 0, logging a harmless but noisy "Unknown
    // GetPhysMemRegionInfo" error. Point it just past the color buffer instead; nothing ever reads
    // or writes through it since depth/stencil testing never runs.
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.depth_buffer_address),
             (render_target_addr + RenderTargetBytes) / 8);
    cmd.Write(PICA_REG_INDEX(framebuffer.framebuffer.width),
             RenderTargetSize | ((RenderTargetSize - 1) << 12));

    // texture unit 0: RGBA8, ClampToEdge/Nearest (all zero defaults), TextureSize square.
    cmd.Write(PICA_REG_INDEX(texturing.main_config.texture0_enable), 1);
    cmd.Write(PICA_REG_INDEX(texturing.texture0.address), texture_addr / 8);
    cmd.Write(PICA_REG_INDEX(texturing.texture0.height),
             TextureSize | (TextureSize << 16));
    cmd.Write(PICA_REG_INDEX(texturing.texture0_format), 0); // RGBA8

    // TEV stage 0: replace with Texture0 as the sole source (default op is already Replace).
    // color_source1 (bits 0-3) = alpha_source1 (bits 16-19) = Source::Texture0 (3).
    cmd.Write(PICA_REG_INDEX(texturing.tev_stage0.sources_raw), (3u << 16) | 3u);
    WritePassthroughTevChain(cmd);

    cmd.Write(PICA_REG_INDEX(pipeline.max_input_attrib_index), 1);
    cmd.Write(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.index), 0xF);

    const auto push_vertex = [&](float x, float y, float u, float v) {
        const auto pos = PackImmediateF24(x, y, 0.0f, 1.0f);
        for (u32 word : pos) {
            cmd.Write(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]), word);
        }
        const auto tc = PackImmediateF24(u, v, 0.0f, 0.0f);
        for (u32 word : tc) {
            cmd.Write(PICA_REG_INDEX(pipeline.vs_default_attributes_setup.set_value[0]), word);
        }
    };

    push_vertex(-1.0f, -1.0f, 0.0f, 0.0f);
    push_vertex(+1.0f, -1.0f, 1.0f, 0.0f);
    push_vertex(-1.0f, +1.0f, 0.0f, 1.0f);
    push_vertex(+1.0f, -1.0f, 1.0f, 0.0f);
    push_vertex(+1.0f, +1.0f, 1.0f, 1.0f);
    push_vertex(-1.0f, +1.0f, 0.0f, 1.0f);

    return cmd.Bytes();
}

} // namespace

GroupResult RunTexturedQuadGroup() {
    GpuStack stack;
    constexpr Common::Vec4<u8> TexelColor{0x00, 0x00, 0xFF, 0xFF}; // pure blue: exact round trip

    // Every texel identical: any texture tiling/addressing scheme still reads this same color back,
    // so this group never has to reproduce PICA's texture Morton layout to know what to expect.
    u8* texture = Ptr(stack.memory, TexturePAddr);
    for (u32 i = 0; i < TextureSize * TextureSize; ++i) {
        Common::Color::EncodeRGBA8(TexelColor, texture + i * 4);
    }

    FillRegion(stack.memory, TexturedTargetPAddr, RenderTargetBytes, 0xAA);

    const auto command_list = BuildTexturedQuadCommandList(TexturedTargetPAddr, TexturePAddr);
    u8* list_dest = Ptr(stack.memory, GuestListPAddr);
    std::memcpy(list_dest, command_list.data(), command_list.size());
    stack.pica.ProcessCmdList(GuestListPAddr, static_cast<u32>(command_list.size()), false);

    bool passed = true;
    for (u32 y = 0; y < RenderTargetSize && passed; ++y) {
        for (u32 x = 0; x < RenderTargetSize && passed; ++x) {
            const u32 offset = VideoCore::GetMortonOffset(x, y, 4) +
                               (y & ~7u) * RenderTargetSize * 4;
            const u8* pixel = Ptr(stack.memory, TexturedTargetPAddr) + offset;
            // See RunTriangleRasterGroup's comment: decode rather than assume a byte layout.
            if (Common::Color::DecodeRGBA8(pixel) != TexelColor) {
                passed = false;
            }
        }
    }

    return {"textured_quad", passed, Mix(2166136261U, passed ? 1 : 0)};
}

namespace {

/// Bridges the guest homebrew's one new SVC straight into Pica::PicaCore::ProcessCmdList - a stand
/// in for GSP's real "submit GX command" IPC path, which is out of scope until a GSP HLE service
/// exists (see docs/vita-port.md). Otherwise identical in shape to system_corpus.cpp's
/// SystemEnvironment: two-phase setup (Set* after construction), a tiny hand-written SVC table.
class GraphicsEnvironment final : public Core::MemoryEnvironment, public Core::DynComEnvironment {
public:
    static constexpr u32 SvcSubmitGpuCommandList = 0x50;
    static constexpr u32 SvcExitProcess = 0x03;

    explicit GraphicsEnvironment(Pica::PicaCore& pica_) : pica(pica_) {}

    void SetMemory(Memory::MemorySystem& memory_) {
        memory = &memory_;
    }
    void SetCpu(Core::ARM_Interface* cpu_) {
        cpu = cpu_;
    }

    bool IsPoweredOn() const override {
        return true;
    }
    VAddr GetRunningCorePC() const override {
        return cpu != nullptr ? cpu->GetPC() : 0;
    }
    std::shared_ptr<Kernel::Process> GetCurrentProcess() const override {
        return nullptr;
    }
    void FlushRegion(PAddr, u32) override {}
    void InvalidateRegion(PAddr, u32) override {}
    void FlushAndInvalidateRegion(PAddr, u32) override {}
    u32 ReadIoRegister32(VAddr) override {
        return 0;
    }
    void WriteIoRegister32(VAddr, u32) override {}
    void LogUnmappedAccess(Core::ExceptionType) override {
        invalid_access = true;
    }

    u8 ReadMemory8(VAddr address) override {
        return memory->Read8(address);
    }
    u16 ReadMemory16(VAddr address) override {
        return memory->Read16(address);
    }
    u32 ReadMemory32(VAddr address) override {
        return memory->Read32(address);
    }
    u64 ReadMemory64(VAddr address) override {
        return memory->Read64(address);
    }
    void WriteMemory8(VAddr address, u8 value) override {
        memory->Write8(address, value);
    }
    void WriteMemory16(VAddr address, u16 value) override {
        memory->Write16(address, value);
    }
    void WriteMemory32(VAddr address, u32 value) override {
        memory->Write32(address, value);
    }
    void WriteMemory64(VAddr address, u64 value) override {
        memory->Write64(address, value);
    }
    void AddTicks(u64) override {}

    void CallSVC(u32 number) override {
        svc_count++;
        switch (number) {
        case SvcSubmitGpuCommandList: {
            // r0/r1 carry the guest's chosen (vaddr, size), matching the register ABI comment in
            // system_corpus.cpp - but this probe's command list bytes are placed directly at a
            // fixed physical scratch address (GuestListPAddr, see RunGuestFrameGroup) rather than
            // resolved through the process's page table, so the guest's vaddr argument is only
            // used for the byte count; a real GSP-backed implementation would translate it instead.
            const u32 list_size = cpu->GetReg(1);
            pica.ProcessCmdList(GuestListPAddr, list_size, false);
            command_list_submitted = true;
            break;
        }
        case SvcExitProcess:
            exit_process_called = true;
            break;
        default:
            LOG_ERROR(Core_ARM11, "PROBE unhandled SVC 0x{:02X}", number);
            break;
        }
    }

    bool invalid_access{};
    u32 svc_count{};
    bool exit_process_called{};
    bool command_list_submitted{};

private:
    Pica::PicaCore& pica;
    Memory::MemorySystem* memory{};
    Core::ARM_Interface* cpu{};
};

/// Builds a tiny synthetic 3DSX homebrew whose entire job is to call one SVC with a fixed
/// (vaddr, size) pair and then exit - no relocations, exactly like Hito 3's BuildTestHomebrew.
/// Kept separate from Vita::SystemProbe::BuildTestHomebrew (vita/src/system_corpus.cpp) rather than
/// extending it in place, so this milestone cannot change that already-validated Hito 3 corpus.
std::vector<u8> BuildGraphicsHomebrew(u32 command_list_size) {
    const std::array<u32, 7> code_words{{
        0xE59F000C, // ldr r0, [pc, #12]         ; r0 = list vaddr (GraphicsListVAddr, below)
        0xE59F100C, // ldr r1, [pc, #12]         ; r1 = list size
        0xEF000050, // svc #0x50 (SubmitGpuCommandList)
        0xEF000003, // svc #0x03 (ExitProcess)
        0xEAFFFFFE, // b . (should never be reached)
        0x00101000u, // GraphicsListVAddr - one page (0x1000) after code, per 3DSX segment
                     // alignment (core/loader/3dsx_image.cpp rounds every segment up to 0x1000)
        command_list_size,
    }};

    Loader::THREEDSX_Header header{};
    header.magic = 0x58534433; // '3','D','S','X'
    header.header_size = sizeof(Loader::THREEDSX_Header);
    header.reloc_hdr_size = 0;
    header.format_ver = 0;
    header.flags = 0;
    header.code_seg_size = static_cast<u32>(code_words.size() * sizeof(u32));
    header.rodata_seg_size = 0x10; // unused; the command list itself lives in physical scratch
    header.data_seg_size = 0x10;
    header.bss_size = 0x10;
    header.smdh_offset = 0;
    header.smdh_size = 0;
    header.fs_offset = 0;

    std::vector<u8> image;
    image.reserve(sizeof(header) + header.code_seg_size + header.rodata_seg_size);
    const auto* header_bytes = reinterpret_cast<const u8*>(&header);
    image.insert(image.end(), header_bytes, header_bytes + sizeof(header));
    const auto* code_bytes = reinterpret_cast<const u8*>(code_words.data());
    image.insert(image.end(), code_bytes, code_bytes + header.code_seg_size);
    image.insert(image.end(), header.rodata_seg_size, 0);
    return image;
}

/// Writes one screen to a binary PPM (P6), already transposed to landscape orientation the same
/// way src/citra_libretro/emu_window/libretro_window.cpp's blit_screen does: ScreenInfo stores
/// pixels column-major (see renderer_software.cpp's LoadFBToScreenInfo), so a straight row-major PPM
/// write at (x=info.height, y=info.width) is the transpose, with no rotation/scaling arithmetic to
/// get subtly wrong. Alpha is dropped (PPM has no alpha channel); RGB order matches ScreenInfo's.
bool WritePpm(const std::string& path, const SwRenderer::ScreenInfo& info) {
    if (info.pixels.size() < static_cast<std::size_t>(info.width) * info.height * 4) {
        return false;
    }
    FileUtil::IOFile out(path, "wb");
    if (!out.IsOpen()) {
        return false;
    }
    const u32 landscape_width = info.height;
    const u32 landscape_height = info.width;
    const std::string header =
        "P6\n" + std::to_string(landscape_width) + " " + std::to_string(landscape_height) +
        "\n255\n";
    if (out.WriteBytes(header.data(), header.size()) != header.size()) {
        return false;
    }
    std::vector<u8> row(static_cast<std::size_t>(landscape_width) * 3);
    for (u32 y = 0; y < landscape_height; ++y) {
        for (u32 x = 0; x < landscape_width; ++x) {
            const std::size_t src_offset = (static_cast<std::size_t>(y) * info.height + x) * 4;
            row[x * 3 + 0] = info.pixels[src_offset + 0];
            row[x * 3 + 1] = info.pixels[src_offset + 1];
            row[x * 3 + 2] = info.pixels[src_offset + 2];
        }
        if (out.WriteBytes(row.data(), row.size()) != row.size()) {
            return false;
        }
    }
    return true;
}

bool WriteScreenCaptures(const std::string& work_dir, const SwRenderer::RendererSoftware& renderer) {
    const bool top_ok =
        WritePpm(work_dir + "hito5-top.ppm", renderer.Screen(VideoCore::ScreenId::TopLeft));
    const bool bottom_ok =
        WritePpm(work_dir + "hito5-bottom.ppm", renderer.Screen(VideoCore::ScreenId::Bottom));
    return top_ok && bottom_ok;
}

} // namespace

GroupResult RunGuestFrameGroup(const std::string& work_dir, GuestFrameCapture* capture) {
    constexpr Common::Vec4<u8> FlatColor{0x00, 0xFF, 0x00, 0xFF}; // pure green

    Settings::values.is_new_3ds.SetValue(false);
    ForceInterpreterShaderEngine();

    ProbeGpuEnvironment gpu_environment;
    Memory::MemorySystem memory(gpu_environment);
    gpu_environment.SetMemory(memory);
    Pica::PicaCore pica(memory, nullptr);
    SwRenderer::RendererSoftware renderer(gpu_environment, pica);
    SwRenderer::SwBlitter blitter(memory, renderer.Rasterizer());
    pica.BindRasterizer(renderer.Rasterizer());

    FillRegion(memory, GuestTargetPAddr, RenderTargetBytes, 0xAA);
    const auto command_list = BuildFlatQuadCommandList(GuestTargetPAddr, FlatColor);
    std::memcpy(Ptr(memory, GuestListPAddr), command_list.data(), command_list.size());

    const std::string homebrew_path = work_dir + "hito5-homebrew.3dsx";
    {
        FileUtil::IOFile out(homebrew_path, "wb");
        const std::vector<u8> homebrew = BuildGraphicsHomebrew(
            static_cast<u32>(command_list.size()));
        out.WriteBytes(homebrew.data(), homebrew.size());
    }

    Core::Timing timing(1, 100);
    GraphicsEnvironment environment(pica);
    environment.SetMemory(memory);
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::Prod, 1);

    // The CPU must be registered (SetCPUs/SetRunningCPU) before any process becomes current - see
    // system_corpus.cpp's RunSystemCycle, which establishes this order: KernelSystem::
    // SetRunningCPU reads its own current_cpu member before overwriting it (kernel.cpp), which is
    // still null on this first call, but only dereferenced when a current process already exists.
    auto cpu = std::make_shared<Core::ARM_DynCom>(environment, USER32MODE, 0, timing.GetTimer(0));
    environment.SetCpu(cpu.get());
    kernel.SetCPUs({cpu});
    kernel.SetRunningCPU(cpu.get());

    bool memory_map_ok = false;
    std::shared_ptr<Kernel::Process> process;
    {
        FileUtil::IOFile file(homebrew_path, "rb");
        std::shared_ptr<Kernel::CodeSet> codeset;
        const Loader::ThreeDSXResult result =
            Loader::Load3DSXImage(kernel, &file, Memory::PROCESS_IMAGE_VADDR, &codeset);
        if (result == Loader::ThreeDSXResult::Success) {
            process = kernel.CreateProcess(std::move(codeset));
            process->Set3dsxKernelCaps();
            process->resource_limit =
                kernel.ResourceLimit().GetForCategory(Kernel::ResourceLimitCategory::Application);
            process->Run(48, Kernel::DEFAULT_STACK_SIZE);
            kernel.SetCurrentProcess(process);
            memory_map_ok = memory.IsValidVirtualAddress(*process, Memory::PROCESS_IMAGE_VADDR);
        }
    }

    bool entry_point_ok = false;
    if (process) {
        kernel.GetThreadManager(0).Reschedule();
        entry_point_ok = cpu->GetPC() == Memory::PROCESS_IMAGE_VADDR;

        constexpr int MaxSteps = 64;
        for (int i = 0; i < MaxSteps && !environment.exit_process_called &&
                        process->status != Kernel::ProcessStatus::Exited;
             ++i) {
            cpu->Step();
        }
    }

    const bool guest_ok = memory_map_ok && entry_point_ok && environment.command_list_submitted &&
                          environment.exit_process_called && !environment.invalid_access;

    // Present: point the LCD's top-screen framebuffer straight at the guest's render target (same
    // physical address, same RGBA8 format) rather than issuing a second transfer - the
    // transfer_engine group already exercises that mechanism on its own, and a real GSP-backed
    // present would only add a byte-for-byte copy on top of what this already proves. A real
    // homebrew's render target and LCD framebuffer addresses usually differ (see docs/vita-port.md);
    // aliasing them here is this corpus's simplification, not a claim about real hardware layout.
    auto& top = pica.regs.framebuffer_config[0];
    top.width.Assign(RenderTargetSize);
    top.height.Assign(RenderTargetSize);
    top.stride = RenderTargetSize * 4;
    top.color_format.Assign(Pica::PixelFormat::RGBA8);
    top.address_left1 = GuestTargetPAddr;
    top.active_fb = 0;
    pica.regs_lcd.color_fill_top.is_enabled.Assign(0);
    pica.regs_lcd.color_fill_bottom.is_enabled.Assign(1);
    pica.regs_lcd.color_fill_bottom.color_r.Assign(FlatColor.r());
    pica.regs_lcd.color_fill_bottom.color_g.Assign(FlatColor.g());
    pica.regs_lcd.color_fill_bottom.color_b.Assign(FlatColor.b());

    renderer.SwapBuffers();
    const auto& top_screen = renderer.Screen(VideoCore::ScreenId::TopLeft);
    const bool top_ok = top_screen.pixels.size() >= 4 && top_screen.pixels[0] == FlatColor.r() &&
                        top_screen.pixels[1] == FlatColor.g() &&
                        top_screen.pixels[2] == FlatColor.b();

    // Written unconditionally, the same way the homebrew image above is written to work_dir + a
    // plain filename regardless of whether work_dir is empty (see system_corpus.cpp's RunSystemCycle
    // for the same convention: an empty work_dir just means "this host's current directory").
    const bool capture_ok = WriteScreenCaptures(work_dir, renderer);

    if (capture != nullptr) {
        const auto& bottom_screen = renderer.Screen(VideoCore::ScreenId::Bottom);
        capture->top = {top_screen.width, top_screen.height, top_screen.pixels};
        capture->bottom = {bottom_screen.width, bottom_screen.height, bottom_screen.pixels};
    }

    const bool passed = guest_ok && top_ok && capture_ok;
    u32 signature = 2166136261U;
    signature = Mix(signature, guest_ok ? 1 : 0);
    signature = Mix(signature, top_ok ? 1 : 0);
    return {"guest_frame", passed, signature};
}

RenderReport RunRenderCorpus(const std::string& work_dir) {
    RenderReport report{};
    report.groups[report.group_count++] = RunRendererInitGroup();
    report.groups[report.group_count++] = RunColorFillGroup();
    report.groups[report.group_count++] = RunFramebufferFormatsGroup();
    report.groups[report.group_count++] = RunTransferEngineGroup();
    report.groups[report.group_count++] = RunTriangleRasterGroup();
    report.groups[report.group_count++] = RunTexturedQuadGroup();
    report.groups[report.group_count++] = RunGuestFrameGroup(work_dir);
    return report;
}

} // namespace Vita::RenderProbe
