// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/memory_environment.h"

#include "core/arm/arm_interface.h"
#include "core/arm/exception_handler.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

namespace Core {
namespace {

/// Adapts Core::System to the MemoryEnvironment seam so desktop builds behave exactly as before.
class SystemMemoryEnvironment final : public MemoryEnvironment {
public:
    explicit SystemMemoryEnvironment(System& system_) : system(system_) {}

    bool IsPoweredOn() const override {
        return system.IsPoweredOn();
    }

    VAddr GetRunningCorePC() const override {
        return system.GetRunningCore().GetPC();
    }

    std::shared_ptr<Kernel::Process> GetCurrentProcess() const override {
        return system.Kernel().GetCurrentProcess();
    }

    void FlushRegion(PAddr address, u32 size) override {
        system.GPU().Renderer().Rasterizer()->FlushRegion(address, size);
    }

    void InvalidateRegion(PAddr address, u32 size) override {
        system.GPU().Renderer().Rasterizer()->InvalidateRegion(address, size);
    }

    void FlushAndInvalidateRegion(PAddr address, u32 size) override {
        system.GPU().Renderer().Rasterizer()->FlushAndInvalidateRegion(address, size);
    }

    u32 ReadIoRegister32(VAddr address) override {
        return system.GPU().ReadReg(address);
    }

    void WriteIoRegister32(VAddr address, u32 value) override {
        system.GPU().WriteReg(address, value);
    }

    void LogUnmappedAccess(ExceptionType type) override {
        LogException(system, type);
    }

private:
    System& system;
};

} // namespace

std::unique_ptr<MemoryEnvironment> MakeSystemMemoryEnvironment(System& system) {
    return std::make_unique<SystemMemoryEnvironment>(system);
}

} // namespace Core
