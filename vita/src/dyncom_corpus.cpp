// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "dyncom_corpus.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <type_traits>
#include "core/arm/dyncom/arm_dyncom_environment.h"
#include "core/arm/dyncom/arm_dyncom_interpreter.h"
#include "core/arm/dyncom/arm_dyncom_trans.h"
#include "core/arm/skyeye_common/armstate.h"

namespace Vita::DyncomProbe {
namespace {

constexpr VAddr CodeAddress = 0x1000;
constexpr VAddr SecondCodeAddress = 0x1800;
constexpr VAddr DataAddress = 0x2000;
constexpr std::size_t MemorySize = 16 * 1024;

class FlatEnvironment final : public Core::DynComEnvironment {
public:
    u8 ReadMemory8(VAddr address) override {
        return Read<u8>(address);
    }

    u16 ReadMemory16(VAddr address) override {
        return Read<u16>(address);
    }

    u32 ReadMemory32(VAddr address) override {
        return Read<u32>(address);
    }

    u64 ReadMemory64(VAddr address) override {
        return Read<u64>(address);
    }

    void WriteMemory8(VAddr address, u8 value) override {
        Write(address, value);
    }

    void WriteMemory16(VAddr address, u16 value) override {
        Write(address, value);
    }

    void WriteMemory32(VAddr address, u32 value) override {
        Write(address, value);
    }

    void WriteMemory64(VAddr address, u64 value) override {
        Write(address, value);
    }

    void AddTicks(u64 ticks) override {
        tick_count += ticks;
    }

    void CallSVC(u32 number) override {
        last_svc = number;
        ++svc_count;
    }

    void LoadArm(VAddr address, std::initializer_list<u32> instructions) {
        for (const u32 instruction : instructions) {
            Write(address, instruction);
            address += sizeof(instruction);
        }
    }

    void LoadThumb(VAddr address, std::initializer_list<u16> instructions) {
        for (const u16 instruction : instructions) {
            Write(address, instruction);
            address += sizeof(instruction);
        }
    }

    [[nodiscard]] bool HadInvalidAccess() const {
        return invalid_access;
    }

    u64 tick_count{};
    u32 last_svc{};
    u32 svc_count{};

private:
    template <typename T>
    T Read(VAddr address) {
        static_assert(std::is_unsigned_v<T>);
        if (address > memory.size() || memory.size() - address < sizeof(T)) {
            invalid_access = true;
            return 0;
        }

        T value{};
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<T>(memory[address + i]) << (i * 8);
        }
        return value;
    }

    template <typename T>
    void Write(VAddr address, T value) {
        static_assert(std::is_unsigned_v<T>);
        if (address > memory.size() || memory.size() - address < sizeof(T)) {
            invalid_access = true;
            return;
        }

        for (std::size_t i = 0; i < sizeof(T); ++i) {
            memory[address + i] = static_cast<u8>(value >> (i * 8));
        }
    }

    std::array<u8, MemorySize> memory{};
    bool invalid_access{};
};

u64 Execute(ARMul_State& state, u64 instruction_count) {
    state.NumInstrsToExecute = instruction_count;
    return InterpreterMainLoop(&state);
}

void PrepareArm(ARMul_State& state, VAddr address = CodeAddress) {
    state.Reg.fill(0);
    state.Reg[15] = address;
    state.Cpsr = USER32MODE;
    state.TFlag = 0;
    state.instruction_cache.clear();
    trans_cache_buf_top = 0;
}

void PrepareThumb(ARMul_State& state, VAddr address = CodeAddress) {
    PrepareArm(state, address);
    state.Cpsr |= TBIT;
    state.TFlag = 1;
}

u32 Mix(u32 signature, u32 value) {
    return (signature ^ value) * 16777619U;
}

GroupResult ArmAluFlags(FlatEnvironment& environment) {
    ARMul_State state{environment, USER32MODE};
    PrepareArm(state);
    environment.LoadArm(CodeAddress,
                        {0xE3A00005, 0xE3A01003, 0xE0802001, 0xE2513005, 0xE0204001,
                         0xE1805001, 0xE0006001});

    const u64 executed = Execute(state, 7);
    const u32 flags = state.Cpsr & 0xF0000000;
    const bool passed = executed == 7 && state.Reg[0] == 5 && state.Reg[1] == 3 &&
                        state.Reg[2] == 8 && state.Reg[3] == 0xFFFFFFFE && state.Reg[4] == 6 &&
                        state.Reg[5] == 7 && state.Reg[6] == 1 && flags == NBIT;
    u32 signature = Mix(2166136261U, state.Reg[2]);
    signature = Mix(signature, state.Reg[3]);
    signature = Mix(signature, state.Reg[4]);
    signature = Mix(signature, state.Reg[5]);
    signature = Mix(signature, state.Reg[6]);
    signature = Mix(signature, flags);
    return {"arm_alu_flags", passed, signature, executed};
}

GroupResult ArmBranchCall(FlatEnvironment& environment) {
    ARMul_State state{environment, USER32MODE};
    PrepareArm(state);
    environment.LoadArm(CodeAddress,
                        {0xE3A00001, 0xEB000001, 0xE2800004, 0xEA000001, 0xE2800002,
                         0xE12FFF1E, 0xE1A01000});

    const u64 executed = Execute(state, 7);
    const bool passed = executed == 7 && state.Reg[0] == 7 && state.Reg[1] == 7 &&
                        state.Reg[14] == CodeAddress + 8 && state.Reg[15] == CodeAddress + 28;
    u32 signature = Mix(2166136261U, state.Reg[0]);
    signature = Mix(signature, state.Reg[1]);
    signature = Mix(signature, state.Reg[14]);
    signature = Mix(signature, state.Reg[15]);
    return {"arm_branch_call", passed, signature, executed};
}

GroupResult ArmMemory(FlatEnvironment& environment) {
    ARMul_State state{environment, USER32MODE};
    PrepareArm(state);
    state.Reg[0] = DataAddress;
    state.Reg[1] = 0x12345678;
    state.Reg[3] = 0xAB;
    state.Reg[5] = 0xCDEF;
    environment.LoadArm(CodeAddress,
                        {0xE5801000, 0xE5902000, 0xE5C03004, 0xE5D04004, 0xE1C050B6,
                         0xE1D060B6});

    const u64 executed = Execute(state, 6);
    const u32 word = environment.ReadMemory32(DataAddress);
    const u8 byte = environment.ReadMemory8(DataAddress + 4);
    const u16 half = environment.ReadMemory16(DataAddress + 6);
    const bool passed = executed == 6 && state.Reg[2] == 0x12345678 && state.Reg[4] == 0xAB &&
                        state.Reg[6] == 0xCDEF && word == 0x12345678 && byte == 0xAB &&
                        half == 0xCDEF;
    u32 signature = Mix(2166136261U, state.Reg[2]);
    signature = Mix(signature, state.Reg[4]);
    signature = Mix(signature, state.Reg[6]);
    signature = Mix(signature, word);
    return {"arm_memory", passed, signature, executed};
}

GroupResult ThumbAluMemory(FlatEnvironment& environment) {
    ARMul_State state{environment, USER32MODE};
    PrepareThumb(state);
    state.Reg[1] = DataAddress;
    environment.LoadThumb(CodeAddress, {0x2005, 0x3003, 0x3808, 0x6008, 0x680A, 0xDF2A});

    const u64 ticks_before = environment.tick_count;
    const u64 executed_after_svc = Execute(state, 6);
    const u64 executed = executed_after_svc + environment.tick_count - ticks_before;
    const u32 flags = state.Cpsr & 0xF0000000;
    const u32 word = environment.ReadMemory32(DataAddress);
    const bool passed = executed == 6 && state.Reg[0] == 0 && state.Reg[2] == 0 &&
                        flags == (ZBIT | CBIT) && word == 0 && environment.svc_count == 1 &&
                        environment.last_svc == 0x2A;
    u32 signature = Mix(2166136261U, state.Reg[0]);
    signature = Mix(signature, state.Reg[2]);
    signature = Mix(signature, flags);
    signature = Mix(signature, environment.last_svc);
    return {"thumb_alu_memory_svc", passed, signature, executed};
}

GroupResult ThumbBranches(FlatEnvironment& environment) {
    ARMul_State state{environment, USER32MODE};
    PrepareThumb(state);
    environment.LoadThumb(CodeAddress, {0x2000, 0x2800, 0xD101, 0x3001, 0xE000, 0x3004, 0x2107});

    const u64 executed = Execute(state, 6);
    const bool passed = executed == 6 && state.Reg[0] == 1 && state.Reg[1] == 7 &&
                        state.Reg[15] == CodeAddress + 14;
    u32 signature = Mix(2166136261U, state.Reg[0]);
    signature = Mix(signature, state.Reg[1]);
    signature = Mix(signature, state.Reg[15]);
    return {"thumb_branches", passed, signature, executed};
}

GroupResult ContextRoundTrip(FlatEnvironment& environment) {
    ARMul_State state{environment, USER32MODE};
    PrepareArm(state);
    state.Reg[0] = 0x11223344;
    state.Reg[13] = 0x3F00;
    state.Reg[14] = 0x55667788;
    state.ExtReg[3] = 0xA5A55A5A;
    state.VFP[VFP_FPSCR] = 0x01000000;

    const auto registers = state.Reg;
    const auto vfp_registers = state.ExtReg;
    const u32 cpsr = state.Cpsr;
    state.Reset();
    state.Reg = registers;
    state.ExtReg = vfp_registers;
    state.Cpsr = cpsr;
    const bool passed = state.Reg[0] == 0x11223344 && state.Reg[13] == 0x3F00 &&
                        state.Reg[14] == 0x55667788 && state.ExtReg[3] == 0xA5A55A5A;
    u32 signature = Mix(2166136261U, state.Reg[0]);
    signature = Mix(signature, state.Reg[13]);
    signature = Mix(signature, state.Reg[14]);
    signature = Mix(signature, state.ExtReg[3]);
    return {"register_context", passed, signature, 0};
}

GroupResult SharedMemoryCores(FlatEnvironment& environment) {
    ARMul_State producer{environment, USER32MODE};
    ARMul_State consumer{environment, USER32MODE};
    PrepareArm(producer, CodeAddress);
    producer.Reg[0] = DataAddress;
    producer.Reg[1] = 0xCAFEBABE;
    environment.LoadArm(CodeAddress, {0xE5801000});

    PrepareArm(consumer, SecondCodeAddress);
    consumer.Reg[0] = DataAddress;
    consumer.Reg[1] = 0x13579BDF;
    environment.LoadArm(SecondCodeAddress, {0xE5902000});

    const u64 producer_executed = Execute(producer, 1);
    trans_cache_buf_top = 0;
    consumer.instruction_cache.clear();
    const u64 consumer_executed = Execute(consumer, 1);
    const bool passed = producer_executed == 1 && consumer_executed == 1 &&
                        producer.Reg[1] == 0xCAFEBABE && consumer.Reg[1] == 0x13579BDF &&
                        consumer.Reg[2] == 0xCAFEBABE;
    u32 signature = Mix(2166136261U, producer.Reg[1]);
    signature = Mix(signature, consumer.Reg[1]);
    signature = Mix(signature, consumer.Reg[2]);
    return {"shared_memory_cores", passed, signature, producer_executed + consumer_executed};
}

} // Anonymous namespace

bool CorpusReport::Passed() const {
    return !invalid_access && group_count == groups.size() &&
           std::all_of(groups.begin(), groups.end(), [](const GroupResult& group) {
               return group.passed;
           });
}

CorpusReport RunCorpus() {
    FlatEnvironment environment;
    CorpusReport report;
    report.groups[report.group_count++] = ArmAluFlags(environment);
    report.groups[report.group_count++] = ArmBranchCall(environment);
    report.groups[report.group_count++] = ArmMemory(environment);
    report.groups[report.group_count++] = ThumbAluMemory(environment);
    report.groups[report.group_count++] = ThumbBranches(environment);
    report.groups[report.group_count++] = ContextRoundTrip(environment);
    report.groups[report.group_count++] = SharedMemoryCores(environment);
    report.invalid_access = environment.HadInvalidAccess();
    for (const GroupResult& group : report.groups) {
        report.instructions += group.instructions;
    }
    return report;
}

BenchmarkResult RunBenchmark(u32 iterations) {
    FlatEnvironment environment;
    ARMul_State state{environment, USER32MODE};
    PrepareArm(state);
    state.Reg[0] = 0;
    state.Reg[1] = iterations;
    environment.LoadArm(CodeAddress, {0xE2800001, 0xE2511001, 0x1AFFFFFC});

    const u64 requested = static_cast<u64>(iterations) * 3;
    const u64 executed = Execute(state, requested);
    return {executed == requested && state.Reg[0] == iterations && state.Reg[1] == 0 &&
                !environment.HadInvalidAccess(),
            executed, state.Reg[0]};
}

} // namespace Vita::DyncomProbe
