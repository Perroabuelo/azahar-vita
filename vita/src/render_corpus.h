// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "common/common_types.h"

namespace Vita::RenderProbe {

struct GroupResult {
    const char* name{};
    bool passed{};
    u32 signature{};
};

/// One screen's pixels, copied out of a SwRenderer::ScreenInfo: width/height and column-major RGBA8
/// exactly as video_core/renderer_software/renderer_software.cpp's LoadFBToScreenInfo produces them
/// (see that file's comment on the column-major layout). Exists so a Vita-only caller (render_main.
/// cpp) can blit the actual rendered frame to the physical screen without render_corpus.cpp itself
/// depending on anything Vita-specific - the desktop reference never populates this.
struct ScreenCapture {
    u32 width{};
    u32 height{};
    std::vector<u8> pixels;
};

struct GuestFrameCapture {
    ScreenCapture top;
    ScreenCapture bottom;
};

struct RenderReport {
    std::array<GroupResult, 7> groups{};
    std::size_t group_count{};

    [[nodiscard]] bool Passed() const;
};

/// Constructs Memory::MemorySystem, Pica::PicaCore and SwRenderer::RendererSoftware over
/// VideoCore::RendererEnvironment without Core::System or Frontend::EmuWindow (see Hito 5's
/// video_core seam, video_core/renderer_environment.h) and checks the trio comes up with the
/// desktop-reference default screen geometry.
GroupResult RunRendererInitGroup();
/// Exercises regs_lcd's color_fill_top/bottom - the same mechanism GSP uses for solid-color splash
/// screens - and checks RendererSoftware::Screen() for both screens after SwapBuffers().
GroupResult RunColorFillGroup();
/// Exercises RendererSoftware::LoadFBToScreenInfo's decode dispatch for all five Pica::PixelFormat
/// values by round-tripping a known pixel through Common::Color::EncodeXXX/DecodeXXX.
GroupResult RunFramebufferFormatsGroup();
/// Exercises SwRenderer::SwBlitter::MemoryFill and ::TextureCopy (the display-transfer engine's
/// texture-copy mode - a raw memcpy between physical addresses, see sw_blitter.cpp) directly, the
/// same mechanism a real homebrew uses to move a render target into the LCD framebuffer.
GroupResult RunTransferEngineGroup();
/// Builds a real PICA raw command list (register writes plus immediate-mode vertex submission) and
/// runs it through Pica::PicaCore::ProcessCmdList and the real SwRenderer::RasterizerSoftware,
/// rendering a flat-shaded full-viewport quad and checking every byte of the render target.
GroupResult RunTriangleRasterGroup();
/// As RunTriangleRasterGroup, but the quad samples a uniformly-colored texture through the texture
/// unit and TEV combiner instead of using the interpolated vertex color.
GroupResult RunTexturedQuadGroup();
/// Loads a synthetic 3DSX homebrew (BuildGraphicsHomebrew below) through the real
/// Loader::Load3DSXImage, Memory::MemorySystem and Kernel::KernelSystem - the same pattern Hito 3's
/// system_corpus.cpp uses - reaches its entry point, and lets the guest's own ARM11 code submit a
/// flat-shaded quad command list through one probe SVC before exiting. The harness then points the
/// LCD's top-screen framebuffer at the guest's render target directly (RunTransferEngineGroup's
/// texture-copy transfer is exercised on its own already, not repeated here - see render_corpus.cpp)
/// and calls RendererSoftware::SwapBuffers(), checking the resulting screen matches the expected
/// flat color. This is the "a graphical homebrew produces a recognizable image" acceptance
/// criterion. As a side
/// effect, writes the top and bottom screens to two binary PPM (P6) files under work_dir, already
/// transposed to landscape orientation, so the desktop reference and the Vita probe can be compared
/// byte for byte (see Hito 5's physical validation) - this is the only group that touches work_dir.
/// When `capture` is non-null, also copies the same two screens into it (see ScreenCapture) so a
/// Vita-only caller can present the actual rendered frame on the physical screen; the desktop
/// reference always passes nullptr.
GroupResult RunGuestFrameGroup(const std::string& work_dir = "", GuestFrameCapture* capture = nullptr);

/// Runs all seven groups above in order. Shared verbatim between the desktop reference and the Vita
/// probe, exactly like Hitos 2-4's corpora.
RenderReport RunRenderCorpus(const std::string& work_dir = "");

} // namespace Vita::RenderProbe
