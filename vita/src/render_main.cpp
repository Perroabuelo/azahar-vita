// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <malloc.h>
#include <span>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "render_corpus.h"

// See system_main.cpp/budget_main.cpp for the newlib heap background. This probe keeps the Hito 4
// figure: it constructs exactly one heap-based Memory::MemorySystem at a time (each group tears its
// own down before the next runs - see render_corpus.cpp's GpuStack and RunGuestFrameGroup), plus
// video_core's own buffers (three SwRenderer::ScreenInfo pixel vectors, a handful of KiB), which fit
// comfortably inside the 32 MiB RendererReserveBytes the Hito 4 budget corpus already reserved for
// this milestone (see vita/src/budget_corpus.cpp's renderer_headroom group).
extern "C" {
unsigned int _newlib_heap_size_user = SCE_KERNEL_128MiB + SCE_KERNEL_32MiB;
}

namespace {

constexpr int ScreenWidth = 960;
constexpr int ScreenHeight = 544;
constexpr int ScreenPitch = 960;
constexpr int FramebufferBytes = ScreenPitch * ScreenHeight * sizeof(std::uint32_t);
constexpr int CdramBlockAlignment = 256 * 1024;
constexpr int FramebufferAllocationBytes =
    (FramebufferBytes + CdramBlockAlignment - 1) / CdramBlockAlignment * CdramBlockAlignment;
constexpr unsigned int FailureDisplayTimeMicroseconds = 5'000'000;
constexpr const char* ProbeVersion = "05.00";
constexpr const char* LogPath = "ux0:data/azahar-vita/boot.log";
constexpr const char* WorkDir = "ux0:data/azahar-vita/hito-5/";

// Where the guest_frame group's two captured screens land on the physical 960x544 panel, each at
// native 3DS resolution (top 400x240, bottom 320x240) with a dark-gray background filling the rest
// - see PresentScreens below.
constexpr int TopScreenX = 280; // (960 - 400) / 2, centered
constexpr int TopScreenY = 16;
constexpr int BottomScreenX = 320; // (960 - 320) / 2, centered
constexpr int BottomScreenY = 288;
constexpr std::uint32_t BackgroundColor = 0xFF202020;

constexpr std::array<std::uint32_t, 6> FailureColors{
    0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000,
};

static_assert(__cplusplus >= 202002L, "The Vita render probe requires C++20");
static_assert(sizeof(void*) == 4, "The Vita render probe must be built for a 32-bit target");

struct MemorySnapshot {
    SceKernelFreeMemorySizeInfo free_memory{};
    struct mallinfo heap {};
    int result{};
};

MemorySnapshot CaptureMemory() {
    MemorySnapshot snapshot{};
    snapshot.free_memory.size = sizeof(snapshot.free_memory);
    snapshot.result = sceKernelGetFreeMemorySize(&snapshot.free_memory);
    snapshot.heap = mallinfo();
    return snapshot;
}

class Framebuffer {
public:
    int Allocate() {
        uid = sceKernelAllocMemBlock("azahar-vita-render-framebuffer",
                                     SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                     FramebufferAllocationBytes, nullptr);
        if (uid < 0) {
            return uid;
        }
        return sceKernelGetMemBlockBase(uid, reinterpret_cast<void**>(&pixels));
    }

    void DrawBands(std::span<const std::uint32_t, 6> colors) const {
        for (int y = 0; y < ScreenHeight; ++y) {
            const auto index = std::min(static_cast<std::size_t>(y * colors.size() / ScreenHeight),
                                        colors.size() - 1);
            std::fill_n(pixels + y * ScreenPitch, ScreenWidth, colors[index]);
        }
    }

    // Draws one guest_frame capture at (base_x, base_y), transposing its column-major RGBA8 pixels
    // (see render_corpus.h's ScreenCapture and renderer_software.cpp's LoadFBToScreenInfo) into the
    // panel's row-major A8B8G8R8 the same way src/citra_libretro/emu_window/libretro_window.cpp's
    // blit_screen and vita/src/render_corpus.cpp's WritePpm both do: display (dx, dy) reads from
    // ScreenInfo (x=dy, y=dx). landscape_width/landscape_height are info.height/info.width.
    void DrawCapture(int base_x, int base_y, const Vita::RenderProbe::ScreenCapture& capture) const {
        if (capture.pixels.size() <
            static_cast<std::size_t>(capture.width) * capture.height * 4) {
            return;
        }
        const std::uint32_t landscape_width = capture.height;
        const std::uint32_t landscape_height = capture.width;
        for (std::uint32_t dy = 0; dy < landscape_height; ++dy) {
            std::uint32_t* dest_row = pixels + (base_y + dy) * ScreenPitch + base_x;
            for (std::uint32_t dx = 0; dx < landscape_width; ++dx) {
                const std::size_t src_offset =
                    (static_cast<std::size_t>(dy) * capture.height + dx) * 4;
                const std::uint8_t r = capture.pixels[src_offset + 0];
                const std::uint8_t g = capture.pixels[src_offset + 1];
                const std::uint8_t b = capture.pixels[src_offset + 2];
                const std::uint8_t a = capture.pixels[src_offset + 3];
                dest_row[dx] = (static_cast<std::uint32_t>(a) << 24) |
                              (static_cast<std::uint32_t>(b) << 16) |
                              (static_cast<std::uint32_t>(g) << 8) | r;
            }
        }
    }

    int Present() {
        SceDisplayFrameBuf frame{};
        frame.size = sizeof(frame);
        frame.base = pixels;
        frame.pitch = ScreenPitch;
        frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        frame.width = ScreenWidth;
        frame.height = ScreenHeight;
        const int result = sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
        presented = result >= 0;
        return result;
    }

    int Shutdown() {
        int result = 0;
        if (presented) {
            result = sceDisplaySetFrameBuf(nullptr, SCE_DISPLAY_SETBUF_NEXTFRAME);
            if (result >= 0) {
                result = sceDisplayWaitVblankStart();
            }
            presented = false;
        }
        if (uid >= 0) {
            const int free_result = sceKernelFreeMemBlock(uid);
            if (result >= 0) {
                result = free_result;
            }
            uid = -1;
            pixels = nullptr;
        }
        return result;
    }

    [[nodiscard]] bool IsAllocated() const {
        return pixels != nullptr;
    }

    [[nodiscard]] bool IsPresented() const {
        return presented;
    }

    ~Framebuffer() {
        Shutdown();
    }

private:
    SceUID uid{-1};
    std::uint32_t* pixels{};
    bool presented{};
};

int Fail(Framebuffer* framebuffer, const char* stage, int error) {
    LOG_ERROR(Core_ARM11, "FAIL {} code=0x{:08X}", stage, static_cast<std::uint32_t>(error));
    LOG_ERROR(Core_ARM11, "RESULT FAIL");
    if (framebuffer != nullptr && framebuffer->IsAllocated()) {
        framebuffer->DrawBands(FailureColors);
        if (!framebuffer->IsPresented()) {
            static_cast<void>(framebuffer->Present());
        }
        static_cast<void>(sceDisplayWaitVblankStart());
        static_cast<void>(sceKernelDelayThread(FailureDisplayTimeMicroseconds));
    }
    Common::Log::Stop();
    return 1;
}

} // namespace

int main() {
    const MemorySnapshot memory_before = CaptureMemory();
    FileUtil::SetUserPath();
    Common::Log::Initialize("boot.log");
    Common::Log::Start();
    LOG_INFO(Core_ARM11, "render_probe_version={} platform=vita", ProbeVersion);

    Framebuffer framebuffer;
    int result = framebuffer.Allocate();
    if (result < 0) {
        return Fail(nullptr, "cdram_allocate", result);
    }
    // Fill with the background color first (see budget_main.cpp for why six color bands were the
    // liveness signal through Hito 4); this milestone shows the actual rendered guest frame instead,
    // composed onto this same background once the corpus below produces it.
    framebuffer.DrawBands(std::array<std::uint32_t, 6>{BackgroundColor, BackgroundColor,
                                                       BackgroundColor, BackgroundColor,
                                                       BackgroundColor, BackgroundColor});
    result = framebuffer.Present();
    if (result < 0) {
        return Fail(&framebuffer, "display_present", result);
    }
    if (sceDisplayWaitVblankStart() < 0) {
        return Fail(&framebuffer, "display_vblank", -1);
    }
    if (!FileUtil::Exists(LogPath)) {
        return Fail(&framebuffer, "azahar_logger", -1);
    }
    LOG_INFO(Core_ARM11, "PASS logger backend=vita path={}", LogPath);

    if (!FileUtil::CreateFullPath(WorkDir)) {
        return Fail(&framebuffer, "work_dir", -1);
    }

#if defined(AZAHAR_VITA_RENDER_FORCE_FAILURE)
    return Fail(&framebuffer, "forced_failure", static_cast<int>(0xA5000001U));
#endif

    // Called individually (not through RunRenderCorpus) so a progress line can be logged right after
    // each - the software rasterizer with an interpreted vertex shader is slow on real ARMv7
    // hardware, and Hito 4's physical testing showed a quiet probe looks hung rather than working
    // (see docs/vita-port.md). guest_frame additionally receives a capture output so its rendered
    // frame - not color bands - can be shown on the physical screen below.
    Vita::RenderProbe::RenderReport report{};
    report.groups[report.group_count++] = Vita::RenderProbe::RunRendererInitGroup();
    LOG_INFO(Core_ARM11, "PASS render_progress group=renderer_init");
    report.groups[report.group_count++] = Vita::RenderProbe::RunColorFillGroup();
    LOG_INFO(Core_ARM11, "PASS render_progress group=color_fill");
    report.groups[report.group_count++] = Vita::RenderProbe::RunFramebufferFormatsGroup();
    LOG_INFO(Core_ARM11, "PASS render_progress group=framebuffer_formats");
    report.groups[report.group_count++] = Vita::RenderProbe::RunTransferEngineGroup();
    LOG_INFO(Core_ARM11, "PASS render_progress group=transfer_engine");
    report.groups[report.group_count++] = Vita::RenderProbe::RunTriangleRasterGroup();
    LOG_INFO(Core_ARM11, "PASS render_progress group=triangle_raster");
    report.groups[report.group_count++] = Vita::RenderProbe::RunTexturedQuadGroup();
    LOG_INFO(Core_ARM11, "PASS render_progress group=textured_quad");

    Vita::RenderProbe::GuestFrameCapture capture{};
    report.groups[report.group_count++] = Vita::RenderProbe::RunGuestFrameGroup(WorkDir, &capture);
    LOG_INFO(Core_ARM11, "PASS render_progress group=guest_frame");

    for (std::size_t i = 0; i < report.group_count; ++i) {
        const auto& group = report.groups[i];
        if (group.passed) {
            LOG_INFO(Core_ARM11, "PASS group={} signature=0x{:08X}", group.name, group.signature);
        } else {
            LOG_ERROR(Core_ARM11, "FAIL group={} signature=0x{:08X}", group.name, group.signature);
        }
    }
    if (!report.Passed()) {
        return Fail(&framebuffer, "render_corpus", -1);
    }

    framebuffer.DrawCapture(TopScreenX, TopScreenY, capture.top);
    framebuffer.DrawCapture(BottomScreenX, BottomScreenY, capture.bottom);
    result = framebuffer.Present();
    if (result < 0) {
        return Fail(&framebuffer, "display_present_frame", result);
    }
    if (sceDisplayWaitVblankStart() < 0) {
        return Fail(&framebuffer, "display_vblank", -1);
    }
    LOG_INFO(Core_ARM11, "PASS present top={}x{} bottom={}x{}", capture.top.height,
             capture.top.width, capture.bottom.height, capture.bottom.width);

    const MemorySnapshot memory_after = CaptureMemory();
    if (memory_before.result < 0 || memory_after.result < 0) {
        return Fail(&framebuffer, "memory_snapshot",
                    memory_after.result < 0 ? memory_after.result : memory_before.result);
    }
    LOG_INFO(Core_ARM11,
             "PASS memory heap_request={} heap_used={} user_free_before={} user_free_after={} "
             "cdram_free_before={} cdram_free_after={} phycont_free_before={} "
             "phycont_free_after={}",
             _newlib_heap_size_user, memory_after.heap.uordblks, memory_before.free_memory.size_user,
             memory_after.free_memory.size_user, memory_before.free_memory.size_cdram,
             memory_after.free_memory.size_cdram, memory_before.free_memory.size_phycont,
             memory_after.free_memory.size_phycont);

    result = sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    if (result < 0) {
        return Fail(&framebuffer, "controller_mode", result);
    }
    while (true) {
        SceCtrlData controls{};
        result = sceCtrlPeekBufferPositive(0, &controls, 1);
        if (result != 1) {
            return Fail(&framebuffer, "controller_read", result != 0 ? result : -1);
        }
        if ((controls.buttons & SCE_CTRL_START) != 0) {
            break;
        }
        if (sceDisplayWaitVblankStart() < 0) {
            return Fail(&framebuffer, "display_vblank", -1);
        }
    }

    LOG_INFO(Core_ARM11, "PASS controller input=start");
    result = framebuffer.Shutdown();
    if (result < 0) {
        return Fail(nullptr, "framebuffer_shutdown", result);
    }
    LOG_INFO(Core_ARM11, "PASS cleanup framebuffer=released");
    LOG_INFO(Core_ARM11, "RESULT PASS groups={}", report.group_count);
    Common::Log::Stop();
    return 0;
}
