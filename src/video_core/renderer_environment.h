// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

namespace Memory {
class MemorySystem;
}

namespace VideoCore {

/**
 * Abstracts the parts of Core::System and Frontend::EmuWindow that VideoCore::RendererBase needs:
 * the memory system, per-frame perf-stats bookkeeping, frame limiting, and window event polling.
 * This keeps renderer_base.cpp and the software renderer free of a hard dependency on
 * Core::System and Frontend::EmuWindow, which lets RendererSoftware be linked into a minimal
 * harness (such as the Vita port) without them - mirrors Core::MemoryEnvironment (see
 * core/memory_environment.h) and Core::DynComEnvironment.
 *
 * Only used under AZAHAR_VITA; the desktop build keeps RendererBase's direct Core::System &
 * Frontend::EmuWindow references unchanged.
 */
class RendererEnvironment {
public:
    virtual ~RendererEnvironment() = default;

    virtual Memory::MemorySystem& Memory() = 0;

    /// Bracket one frame's presentation for perf-stats purposes. No-ops by default: a harness with
    /// nothing to measure (e.g. a deterministic probe) need not override these.
    virtual void StartSwap() {}
    virtual void EndSwap() {}

    /// Mirrors Core::System::perf_stats's system-frame bracketing done in RendererBase::EndFrame.
    virtual void BeginSystemFrame() {}
    virtual void EndSystemFrame() {}

    /// Paces frame presentation to the emulated system's expected rate. A no-op by default: a
    /// deterministic probe wants every frame processed as fast as possible, not throttled.
    virtual void LimitFrame() {}

    /// Replaces Frontend::EmuWindow::PollEvents, called once per frame from RendererBase::EndFrame.
    virtual void PollEvents() {}

    /// The software renderer always renders at native 3DS resolution (see
    /// RendererBase::GetResolutionScaleFactor), so the default is deliberately 1; a harness has no
    /// reason to override this unless it drives an accelerated renderer.
    virtual u32 GetResolutionScaleFactor() {
        return 1;
    }
};

} // namespace VideoCore
