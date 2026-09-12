// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include "common/common_types.h"
#include "core/arm/exception_handler.h"

namespace Kernel {
class Process;
}

namespace Memory {

/// Identifies one of MemorySystem's four large backing regions. Defined here (rather than in
/// memory.h, where it lived before Hito 4) so Core::MemoryEnvironment::AllocateBackingMemory can
/// use it without memory_environment.h including memory.h back, which already includes this
/// header.
enum class Region { FCRAM, VRAM, DSP, N3DS };

} // namespace Memory

namespace Core {

class System;

/**
 * Abstracts the parts of Core::System that Memory::MemorySystem needs: the running CPU's PC for
 * diagnostics, the rasterizer cache flush/invalidate hooks, GPU MMIO register access for the
 * Luma3DS alias, the current process, unmapped-access reporting, and now (Hito 4) the four large
 * backing-memory allocations themselves. This keeps memory.cpp free of a hard dependency on
 * video_core, audio_core, and Core::System itself, which lets it be linked into a minimal harness
 * (such as the Vita port) without those subsystems.
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

    /// Reserves one of MemorySystem's large backing regions. Returning nullptr is a reportable
    /// failure, not a crash: MemorySystem records it (see MemorySystem::IsInitialized and
    /// GetFailedItem) instead of dereferencing the null result, so a caller can turn it into a
    /// controlled, logged failure. `size` of 0 (e.g. N3DS extra RAM on a Vita Old-3DS-only build,
    /// see core/memory.cpp's N3DS_EXTRA_RAM_ALLOCATED_SIZE) means the region isn't requested at all
    /// and this is never called for it. The default is a plain zero-initialized heap allocation,
    /// which is what every desktop host wants (unmodified pre-Hito-4 behavior); the Vita probes
    /// override this to give each region its own named, individually freeable
    /// sceKernelAllocMemBlock instead of folding all four into the newlib heap.
    virtual u8* AllocateBackingMemory(Memory::Region region, std::size_t size) {
        (void)region;
        return new u8[size]();
    }

    /// Releases a buffer obtained from AllocateBackingMemory. `data` is nullptr when that call
    /// failed or was never made; implementations must treat that as a no-op.
    virtual void FreeBackingMemory(Memory::Region region, u8* data, std::size_t size) {
        (void)region;
        (void)size;
        delete[] data;
    }
};

#if !defined(AZAHAR_VITA)
/// Builds the desktop MemoryEnvironment adapter over a real Core::System. Not linked on Vita,
/// where Memory::MemorySystem is always constructed directly from a caller-supplied environment.
std::unique_ptr<MemoryEnvironment> MakeSystemMemoryEnvironment(System& system);
#endif

} // namespace Core
