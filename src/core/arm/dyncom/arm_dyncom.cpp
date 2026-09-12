// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include <memory>
#include "core/arm/dyncom/arm_dyncom.h"
#include "core/arm/dyncom/arm_dyncom_environment.h"
#include "core/arm/dyncom/arm_dyncom_interpreter.h"
#include "core/arm/dyncom/arm_dyncom_trans.h"
#include "core/arm/skyeye_common/armstate.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/svc.h"
#include "core/memory.h"

namespace Core {

namespace {

class SystemDynComEnvironment final : public DynComEnvironment {
public:
    SystemDynComEnvironment(Core::System& system_, Memory::MemorySystem& memory_)
        : system{system_}, memory{memory_} {}

    u8 ReadMemory8(VAddr address) override {
        return memory.Read8(address);
    }

    u16 ReadMemory16(VAddr address) override {
        return memory.Read16(address);
    }

    u32 ReadMemory32(VAddr address) override {
        return memory.Read32(address);
    }

    u64 ReadMemory64(VAddr address) override {
        return memory.Read64(address);
    }

    void WriteMemory8(VAddr address, u8 value) override {
        memory.Write8(address, value);
    }

    void WriteMemory16(VAddr address, u16 value) override {
        memory.Write16(address, value);
    }

    void WriteMemory32(VAddr address, u32 value) override {
        memory.Write32(address, value);
    }

    void WriteMemory64(VAddr address, u64 value) override {
        memory.Write64(address, value);
    }

    void AddTicks(u64 ticks) override {
        system.GetRunningCore().GetTimer().AddTicks(ticks);
    }

    void CallSVC(u32 number) override {
        Kernel::SVCContext{system}.CallSVC(number);
    }

private:
    Core::System& system;
    Memory::MemorySystem& memory;
};

} // Anonymous namespace

ARM_DynCom::ARM_DynCom(Core::System& system_, Memory::MemorySystem& memory,
                       PrivilegeMode initial_mode, u32 id,
                       std::shared_ptr<Core::Timing::Timer> timer)
    : ARM_Interface(id, timer) {
    environment = std::make_unique<SystemDynComEnvironment>(system_, memory);
    state = std::make_unique<ARMul_State>(*environment, initial_mode);
}

ARM_DynCom::~ARM_DynCom() {}

void ARM_DynCom::Run() {
    if (break_flag) [[unlikely]] {
        return;
    }
    ExecuteInstructions(std::max<s64>(timer->GetDowncount(), 0));
}

void ARM_DynCom::Step() {
    if (break_flag) [[unlikely]] {
        return;
    }
    ExecuteInstructions(1);
}

void ARM_DynCom::ClearInstructionCache() {
    state->instruction_cache.clear();
    trans_cache_buf_top = 0;
}

void ARM_DynCom::InvalidateCacheRange(u32, std::size_t) {
    ClearInstructionCache();
}

void ARM_DynCom::SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) {
    ClearInstructionCache();
}

std::shared_ptr<Memory::PageTable> ARM_DynCom::GetPageTable() const {
    return nullptr;
}

void ARM_DynCom::SetPC(u32 pc) {
    state->Reg[15] = pc;
}

u32 ARM_DynCom::GetPC() const {
    return state->Reg[15];
}

u32 ARM_DynCom::GetReg(int index) const {
    return state->Reg[index];
}

void ARM_DynCom::SetReg(int index, u32 value) {
    state->Reg[index] = value;
}

u32 ARM_DynCom::GetVFPReg(int index) const {
    return state->ExtReg[index];
}

void ARM_DynCom::SetVFPReg(int index, u32 value) {
    state->ExtReg[index] = value;
}

u32 ARM_DynCom::GetVFPSystemReg(VFPSystemRegister reg) const {
    return state->VFP[reg];
}

void ARM_DynCom::SetVFPSystemReg(VFPSystemRegister reg, u32 value) {
    state->VFP[reg] = value;
}

u32 ARM_DynCom::GetCPSR() const {
    return state->Cpsr;
}

void ARM_DynCom::SetCPSR(u32 cpsr) {
    state->Cpsr = cpsr;
}

u32 ARM_DynCom::GetCP15Register(CP15Register reg) const {
    return state->CP15[reg];
}

void ARM_DynCom::SetCP15Register(CP15Register reg, u32 value) {
    state->CP15[reg] = value;
}

void ARM_DynCom::ExecuteInstructions(u64 num_instructions) {
    state->NumInstrsToExecute = num_instructions;
    const u32 ticks_executed = InterpreterMainLoop(state.get());
    if (timer) {
        timer->AddTicks(ticks_executed);
    }
    state->ServeBreak();
}

void ARM_DynCom::SaveContext(ThreadContext& ctx) {
    ctx.cpu_registers = state->Reg;
    ctx.cpsr = state->Cpsr;
    ctx.fpu_registers = state->ExtReg;
    ctx.fpscr = state->VFP[VFP_FPSCR];
    ctx.fpexc = state->VFP[VFP_FPEXC];
}

void ARM_DynCom::LoadContext(const ThreadContext& ctx) {
    state->Reg = ctx.cpu_registers;
    state->Cpsr = ctx.cpsr;
    state->ExtReg = ctx.fpu_registers;
    state->VFP[VFP_FPSCR] = ctx.fpscr;
    state->VFP[VFP_FPEXC] = ctx.fpexc;
}

void ARM_DynCom::PrepareReschedule() {
    state->NumInstrsToExecute = 0;
}

} // namespace Core
