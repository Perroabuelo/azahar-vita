// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "system_corpus.h"

#include <array>
#include <cstring>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/arm/arm_interface.h"
#include "core/arm/dyncom/arm_dyncom.h"
#include "core/arm/dyncom/arm_dyncom_environment.h"
#include "core/core_timing.h"
#include "core/hle/ipc.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/result.h"
#include "core/loader/3dsx_image.h"
#include "core/memory.h"
#include "core/memory_environment.h"

namespace Vita::SystemProbe {
namespace {

// SVC numbers used by the synthetic homebrew below, and their register ABI (derived from
// core/hle/kernel/svc_wrapper.h's GetSVCABI applied to the real SVC::* signatures, not guessed):
//   ConnectToPort(0x2D)     Result(Handle*, VAddr)   in: r1=name addr        out: r0=result, r1=handle
//   SendSyncRequest(0x32)   Result(Handle)           in: r0=handle           out: r0=result
//   OutputDebugString(0x3D) void(VAddr, s32)         in: r0=addr, r1=len     out: (none)
//   ExitProcess(0x03)       void()                   (no registers)
constexpr u32 SvcConnectToPort = 0x2D;
constexpr u32 SvcSendSyncRequest = 0x32;
constexpr u32 SvcOutputDebugString = 0x3D;
constexpr u32 SvcExitProcess = 0x03;

constexpr u32 DataPatternAddr = 0x00102000;
constexpr u32 DataPatternValue = 0xC0DE1234;
constexpr u32 IpcRequestValue = 0x12345678;
constexpr u32 IpcResponseXor = 0xA5A5A5A5;
constexpr char ProbePortName[] = "probe:test";
constexpr char ProbeDebugString[] = "PROBE-HOMEBREW-OK";
constexpr std::size_t ProbeDebugStringLength = sizeof(ProbeDebugString) - 1;

using Kernel::Handle;

/// Responds to the one IPC request the homebrew issues: reads the incoming normal parameter and
/// echoes it back XORed with a fixed constant, so the desktop reference and the Vita probe can
/// both check the exact same value came back through a real ServerSession/HLERequestContext round
/// trip (not just that a handle was obtained).
class ProbeService final : public Kernel::SessionRequestHandler {
public:
    void HandleSyncRequest(Kernel::HLERequestContext& context) override {
        u32* cmd = context.CommandBuffer();
        const u32 header = cmd[0];
        const u32 command_id = header >> 16;
        last_request_value = cmd[1];
        request_count++;
        cmd[0] = IPC::MakeHeader(static_cast<u16>(command_id), 2, 0);
        cmd[1] = 0; // ResultSuccess
        cmd[2] = last_request_value ^ IpcResponseXor;
    }

    u32 last_request_value{};
    u32 request_count{};

protected:
    std::unique_ptr<SessionDataBase> MakeSessionData() override {
        return std::make_unique<SessionDataBase>();
    }
};

/// Implements both environment seams (Core::MemoryEnvironment and Core::DynComEnvironment) needed
/// to link Memory::MemorySystem, Kernel::KernelSystem and Core::ARM_DynCom without Core::System.
/// Memory::MemorySystem and Kernel::KernelSystem each need a reference to this environment before
/// they themselves exist, so the memory/kernel pointers are wired in after construction (SetMemory/
/// SetKernel) rather than taken as constructor parameters; nothing calls back into the environment
/// during MemorySystem's or KernelSystem's own construction, so the two-phase setup is safe. There
/// is no GPU or rasterizer, so the flush/invalidate/MMIO hooks are no-ops; SVC dispatch is a small
/// hand-written table instead of the real Kernel::SVC (see the register ABI comment above).
class SystemEnvironment final : public Core::MemoryEnvironment, public Core::DynComEnvironment {
public:
    void SetMemory(Memory::MemorySystem& memory_) {
        memory = &memory_;
    }
    void SetKernel(Kernel::KernelSystem& kernel_) {
        kernel = &kernel_;
    }
    void SetCpu(Core::ARM_Interface* cpu_) {
        cpu = cpu_;
    }
    void SetThread(std::shared_ptr<Kernel::Thread> thread_) {
        thread = std::move(thread_);
    }

    // Core::MemoryEnvironment
    bool IsPoweredOn() const override {
        return true;
    }

    VAddr GetRunningCorePC() const override {
        return cpu != nullptr ? cpu->GetPC() : 0;
    }

    std::shared_ptr<Kernel::Process> GetCurrentProcess() const override {
        return kernel != nullptr ? kernel->GetCurrentProcess() : nullptr;
    }

    void FlushRegion(PAddr, u32) override {}
    void InvalidateRegion(PAddr, u32) override {}
    void FlushAndInvalidateRegion(PAddr, u32) override {}

    u32 ReadIoRegister32(VAddr) override {
        return 0;
    }
    void WriteIoRegister32(VAddr, u32) override {}

    void LogUnmappedAccess(Core::ExceptionType type) override {
        invalid_access = true;
        LOG_ERROR(Core_ARM11, "PROBE unmapped access type={}", static_cast<u32>(type));
    }

    // Core::DynComEnvironment
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
        case SvcConnectToPort:
            DoConnectToPort();
            break;
        case SvcSendSyncRequest:
            DoSendSyncRequest();
            break;
        case SvcOutputDebugString:
            DoOutputDebugString();
            break;
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
    bool connect_to_port_ok{};
    bool send_sync_request_ok{};
    std::string captured_debug_string;

private:
    void DoConnectToPort() {
        const VAddr port_name_address = cpu->GetReg(1);
        Handle out_handle{};
        Result result(1, ErrorModule::OS, ErrorSummary::InvalidState, ErrorLevel::Permanent);
        const auto current_process = kernel->GetCurrentProcess();
        if (current_process && memory->IsValidVirtualAddress(*current_process, port_name_address)) {
            const std::string port_name = memory->ReadCString(port_name_address, 12);
            const auto it = kernel->named_ports.find(port_name);
            if (it != kernel->named_ports.end()) {
                std::shared_ptr<Kernel::ClientSession> session;
                const Result connect_result = it->second->Connect(std::addressof(session));
                if (connect_result.IsSuccess()) {
                    result = current_process->handle_table.Create(&out_handle, session);
                } else {
                    result = connect_result;
                }
            } else {
                result = Result(2, ErrorModule::OS, ErrorSummary::NotFound, ErrorLevel::Permanent);
            }
        }
        cpu->SetReg(0, result.raw);
        cpu->SetReg(1, out_handle);
        connect_to_port_ok = result.IsSuccess();
    }

    void DoSendSyncRequest() {
        const Handle handle = cpu->GetReg(0);
        Result result(1, ErrorModule::OS, ErrorSummary::InvalidState, ErrorLevel::Permanent);
        if (const auto current_process = kernel->GetCurrentProcess()) {
            auto session = current_process->handle_table.Get<Kernel::ClientSession>(handle);
            if (session && thread) {
                result = session->SendSyncRequest(thread);
            }
        }
        cpu->SetReg(0, result.raw);
        send_sync_request_ok = result.IsSuccess();
    }

    void DoOutputDebugString() {
        const VAddr address = cpu->GetReg(0);
        const s32 len = static_cast<s32>(cpu->GetReg(1));
        captured_debug_string.clear();
        for (s32 i = 0; i < len; ++i) {
            captured_debug_string.push_back(static_cast<char>(memory->Read8(address + i)));
        }
        cpu->SetReg(0, 0);
    }

    Memory::MemorySystem* memory{};
    Kernel::KernelSystem* kernel{};
    Core::ARM_Interface* cpu{};
    std::shared_ptr<Kernel::Thread> thread;
};

u32 Mix(u32 signature, u32 value) {
    return (signature ^ value) * 16777619U;
}

void AppendHeader(std::vector<u8>& image, const Loader::THREEDSX_Header& header) {
    const auto* bytes = reinterpret_cast<const u8*>(&header);
    image.insert(image.end(), bytes, bytes + sizeof(header));
}

} // namespace

std::vector<u8> BuildTestHomebrew() {
    // Hand-assembled ARM (not Thumb) code, produced with `arm-vita-eabi-as`/`objdump` from the
    // equivalent GNU assembler source (see docs/vita-port.md for how to regenerate it) rather than
    // encoded by hand, then frozen here as data. It uses the same TLS/CP15 convention real 3DS
    // homebrew does (mrc p15, 0, r4, c13, c0, 3 to find the command buffer), which only works
    // because Kernel::ThreadManager::Reschedule() lands a real thread context - PC, SP, CPSR, and
    // CP15_THREAD_URO - via Core::ARM_DynCom before this code ever runs.
    constexpr std::array<u32, 25> code_words{{
        0xEE1D4F70, // mrc p15, 0, r4, c13, c0, 3          ; r4 = TLS base
        0xE2844080, // add r4, r4, #0x80                   ; r4 = command buffer address
        0xE59F003C, // ldr r0, [pc, #60]                   ; r0 = DataPatternAddr
        0xE59F103C, // ldr r1, [pc, #60]                   ; r1 = DataPatternValue
        0xE5801000, // str r1, [r0]
        0xE59F1038, // ldr r1, [pc, #56]                   ; r1 = PortNameAddr
        0xEF00002D, // svc #0x2D (ConnectToPort)            ; r0=result, r1=handle
        0xE1A05001, // mov r5, r1                           ; r5 = handle
        0xE59F0030, // ldr r0, [pc, #48]                    ; r0 = IPC header
        0xE5840000, // str r0, [r4]
        0xE59F002C, // ldr r0, [pc, #44]                    ; r0 = IpcRequestValue
        0xE5840004, // str r0, [r4, #4]
        0xE1A00005, // mov r0, r5
        0xEF000032, // svc #0x32 (SendSyncRequest)
        0xE59F0020, // ldr r0, [pc, #32]                    ; r0 = DebugStringAddr
        0xE3A01011, // mov r1, #17
        0xEF00003D, // svc #0x3D (OutputDebugString)
        0xEF000003, // svc #0x03 (ExitProcess)
        0xEAFFFFFE, // b . (should never be reached)
        DataPatternAddr,
        DataPatternValue,
        0x00101000u, // PortNameAddr
        0x00010040u, // IPC::MakeHeader(1, 1, 0)
        IpcRequestValue,
        0x00101010u, // DebugStringAddr
    }};

    std::vector<u8> rodata(0x10 + ProbeDebugStringLength, 0);
    std::memcpy(rodata.data(), ProbePortName, sizeof(ProbePortName) - 1);
    std::memcpy(rodata.data() + 0x10, ProbeDebugString, ProbeDebugStringLength);

    Loader::THREEDSX_Header header{};
    header.magic = 0x58534433; // '3','D','S','X' little-endian
    header.header_size = sizeof(Loader::THREEDSX_Header);
    header.reloc_hdr_size = 0;
    header.format_ver = 0;
    header.flags = 0;
    header.code_seg_size = static_cast<u32>(code_words.size() * sizeof(u32));
    header.rodata_seg_size = static_cast<u32>(rodata.size());
    header.data_seg_size = 0x10;
    header.bss_size = 0x10;
    header.smdh_offset = 0;
    header.smdh_size = 0;
    header.fs_offset = 0;

    std::vector<u8> image;
    image.reserve(sizeof(header) + header.code_seg_size + rodata.size());
    AppendHeader(image, header);
    const auto* code_bytes = reinterpret_cast<const u8*>(code_words.data());
    image.insert(image.end(), code_bytes, code_bytes + header.code_seg_size);
    image.insert(image.end(), rodata.begin(), rodata.end());
    return image;
}

std::vector<u8> BuildCorruptHomebrew() {
    // Too short to even contain a header: Identify3DSXImage fails the magic check (missing bytes)
    // and Load3DSXImage fails the header read, giving both loader entry points a clean,
    // deterministic failure to report instead of reading past the end of the buffer.
    return std::vector<u8>{0x00, 0x00, 0x00, 0x00};
}

bool SystemReport::Passed() const {
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

SystemReport RunSystemCorpus(const std::string& work_dir) {
    SystemReport report{};
    const std::string homebrew_path = work_dir + "hito3-homebrew.3dsx";
    const std::string corrupt_path = work_dir + "hito3-corrupt.3dsx";

    {
        FileUtil::IOFile out(homebrew_path, "wb");
        const std::vector<u8> homebrew = BuildTestHomebrew();
        out.WriteBytes(homebrew.data(), homebrew.size());
    }
    {
        FileUtil::IOFile out(corrupt_path, "wb");
        const std::vector<u8> corrupt = BuildCorruptHomebrew();
        out.WriteBytes(corrupt.data(), corrupt.size());
    }

    // --- Group 1: loader identification ---
    {
        FileUtil::IOFile file(homebrew_path, "rb");
        FileUtil::IOFile corrupt_file(corrupt_path, "rb");
        const bool valid_identified = Loader::Identify3DSXImage(&file);
        const bool corrupt_rejected = !Loader::Identify3DSXImage(&corrupt_file);
        const bool passed = valid_identified && corrupt_rejected;
        report.groups[report.group_count++] = {"loader_identify", passed,
                                                Mix(2166136261U, passed ? 1 : 0)};
    }

    // --- Bring up memory, kernel, CPU and the probe service ---
    // KernelSystem::MemoryInit (hle/kernel/memory.cpp) asserts that the FCRAM region sizes for the
    // chosen MemoryMode sum to exactly FCRAM_SIZE (Old 3DS) or FCRAM_N3DS_SIZE (New 3DS) to match
    // is_new_3ds; MemoryMode::Prod is an Old 3DS layout, and the Vita port's memory budget assumes
    // Old 3DS FCRAM (see core/memory.cpp's FCRAM_ALLOCATED_SIZE), so this must be false here
    // regardless of the setting's default.
    Settings::values.is_new_3ds.SetValue(false);

    Core::Timing timing(1, 100);
    SystemEnvironment environment;
    Memory::MemorySystem memory(environment);
    environment.SetMemory(memory);
    Kernel::KernelSystem kernel(memory, timing, [] {}, Kernel::MemoryMode::Prod, 1);
    environment.SetKernel(kernel);

    // --- Group 2: diagnostics on a deliberately corrupt image ---
    {
        FileUtil::IOFile corrupt_file(corrupt_path, "rb");
        std::shared_ptr<Kernel::CodeSet> codeset;
        const Loader::ThreeDSXResult result =
            Loader::Load3DSXImage(kernel, &corrupt_file, Memory::PROCESS_IMAGE_VADDR, &codeset);
        const bool passed = result == Loader::ThreeDSXResult::ErrorRead;
        report.groups[report.group_count++] = {"diagnostics", passed,
                                                Mix(2166136261U, static_cast<u32>(result))};
    }

    auto cpu = std::make_shared<Core::ARM_DynCom>(environment, USER32MODE, 0, timing.GetTimer(0));
    environment.SetCpu(cpu.get());
    kernel.SetCPUs({cpu});
    kernel.SetRunningCPU(cpu.get());

    auto [server_port, client_port] = kernel.CreatePortPair(1, "probe:test");
    auto probe_service = std::make_shared<ProbeService>();
    server_port->SetHleHandler(probe_service);
    kernel.AddNamedPort("probe:test", client_port);

    // --- Load the homebrew and reach its entry point ---
    bool memory_map_ok = false;
    bool entry_point_ok = false;
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
            memory_map_ok = memory.IsValidVirtualAddress(*process, Memory::PROCESS_IMAGE_VADDR) &&
                           memory.IsValidVirtualAddress(*process, DataPatternAddr);
        }
    }
    report.groups[report.group_count++] = {"memory_and_process", memory_map_ok,
                                            Mix(2166136261U, memory_map_ok ? 1 : 0)};

    if (process) {
        // Ready -> Running: lands PC/SP/CPSR and CP15_THREAD_URO from the thread's saved context.
        kernel.GetThreadManager(0).Reschedule();
        entry_point_ok = cpu->GetPC() == Memory::PROCESS_IMAGE_VADDR;

        const auto thread_list = process->GetThreadList();
        environment.SetThread(thread_list.empty() ? nullptr : thread_list.front());

        constexpr int MaxSteps = 200;
        for (int i = 0; i < MaxSteps && !environment.exit_process_called &&
                        process->status != Kernel::ProcessStatus::Exited;
             ++i) {
            cpu->Step();
        }
    }
    report.groups[report.group_count++] = {"entry_point", entry_point_ok,
                                            Mix(2166136261U, entry_point_ok ? 1 : 0)};

    const bool data_pattern_ok =
        process && memory.Read32(*process, DataPatternAddr) == DataPatternValue;
    const bool svc_ok = environment.connect_to_port_ok && environment.send_sync_request_ok &&
                        environment.exit_process_called && !environment.invalid_access;
    const bool ipc_ok = probe_service->request_count == 1 &&
                        probe_service->last_request_value == IpcRequestValue;
    const bool debug_string_ok = environment.captured_debug_string == ProbeDebugString;
    const bool passed = data_pattern_ok && svc_ok && ipc_ok && debug_string_ok;
    report.groups[report.group_count++] = {
        "svc_and_service_ipc", passed,
        Mix(Mix(Mix(2166136261U, data_pattern_ok ? 1 : 0), ipc_ok ? 1 : 0),
            debug_string_ok ? 1 : 0)};

    return report;
}

} // namespace Vita::SystemProbe
