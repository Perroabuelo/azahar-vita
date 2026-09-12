// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "common/common_types.h"
#include "core/arm/exception_handler.h"

namespace Kernel {
class Process;
}

namespace Core {

class System;

/**
 * Abstracts the parts of Core::System that Memory::MemorySystem needs: the running CPU's PC for
 * diagnostics, the rasterizer cache flush/invalidate hooks, GPU MMIO register access for the
 * Luma3DS alias, the current process, and unmapped-access reporting. This keeps memory.cpp free
 * of a hard dependency on video_core, audio_core, and Core::System itself, which lets it be linked
 * into a minimal harness (such as the Vita port) without those subsystems.
 */
class MemoryEnvironment {
public:
    virtual ~MemoryEnvironment() = default;

    virtual bool IsPoweredOn() const = 0;
    virtual VAddr GetRunningCorePC() const = 0;
    virtual std::shared_ptr<Kernel::Process> GetCurrentProcess() const = 0;

    virtual void FlushRegion(PAddr address, u32 size) = 0;
    virtual void InvalidateRegion(PAddr address, u32 size) = 0;
    virtual void FlushAndInvalidateRegion(PAddr address, u32 size) = 0;

    virtual u32 ReadIoRegister32(VAddr address) = 0;
    virtual void WriteIoRegister32(VAddr address, u32 value) = 0;

    virtual void LogUnmappedAccess(ExceptionType type) = 0;
};

#if !defined(AZAHAR_VITA)
/// Builds the desktop MemoryEnvironment adapter over a real Core::System. Not linked on Vita,
/// where Memory::MemorySystem is always constructed directly from a caller-supplied environment.
std::unique_ptr<MemoryEnvironment> MakeSystemMemoryEnvironment(System& system);
#endif

} // namespace Core
