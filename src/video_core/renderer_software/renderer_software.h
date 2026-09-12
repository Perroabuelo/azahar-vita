// Copyright 2023-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/renderer_base.h"
#include "video_core/renderer_software/sw_rasterizer.h"

namespace Core {
class System;
}

namespace SwRenderer {

struct ScreenInfo {
    u32 width;
    u32 height;
    std::vector<u8> pixels;
};

class RendererSoftware : public VideoCore::RendererBase {
public:
#if defined(AZAHAR_VITA)
    // Drives RendererSoftware directly over a caller-owned VideoCore::RendererEnvironment, without
    // Core::System or Frontend::EmuWindow. Used by harnesses (such as the Vita port) that need the
    // real software renderer without the rest of Core::System.
    explicit RendererSoftware(VideoCore::RendererEnvironment& environment, Pica::PicaCore& pica);
#else
    explicit RendererSoftware(Core::System& system, Pica::PicaCore& pica,
                              Frontend::EmuWindow& window);
#endif
    ~RendererSoftware() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    [[nodiscard]] const ScreenInfo& Screen(VideoCore::ScreenId id) const noexcept {
        return screen_infos[static_cast<u32>(id)];
    }

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override {}

private:
    void PrepareRenderTarget();
    void LoadFBToScreenInfo(int i, const Pica::ColorFill& color_fill);

private:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    RasterizerSoftware rasterizer;
    std::array<ScreenInfo, 3> screen_infos{};
};

} // namespace SwRenderer
