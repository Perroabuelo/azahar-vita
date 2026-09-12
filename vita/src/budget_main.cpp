// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <malloc.h>
#include <span>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include "budget_corpus.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "core/memory.h"
#include "core/memory_environment.h"

// See system_main.cpp for the newlib heap background. Hito 3 needed 192 MiB because FCRAM, VRAM,
// DSP RAM and 4 MiB of New 3DS extra RAM all landed in this same heap. Hito 4 removes the N3DS
// allocation entirely (see core/memory.cpp's N3DS_EXTRA_RAM_ALLOCATED_SIZE) and demonstrates that
// the other three *can* live in their own named sceKernelAllocMemBlock allocations instead (see
// MemblockEnvironment below) - but the deterministic corpus shared with the desktop reference
// (budget_corpus.cpp) still constructs its own Memory::MemorySystem instances through the portable,
// heap-based default environment, exactly as the Hito 2/3 corpora do, so its results stay
// comparable to the desktop build bit for bit. That means this probe's own peak heap need is still
// close to a full ~134.5 MiB (FCRAM+VRAM+DSP) plus the ~5 MiB page table and kernel object
// overhead a loaded homebrew adds - not the dramatically smaller figure a fully memblock-backed
// production system could reach. 160 MiB (down from 192 MiB) is what the N3DS removal and a
// re-measured margin actually support today; a further reduction needs the corpus itself to run
// over a memblock-backed environment, which is future work (see docs/vita-port.md).
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
constexpr const char* ProbeVersion = "04.00";
constexpr const char* LogPath = "ux0:data/azahar-vita/boot.log";
constexpr const char* WorkDir = "ux0:data/azahar-vita/hito-4/";
constexpr std::size_t MemblockAlignment = 4096;

constexpr std::array<std::uint32_t, 6> SuccessColors{
    0xFF202020, 0xFF2774E6, 0xFF2ECC71, 0xFFF1C40F, 0xFFE67E22, 0xFFE74C3C,
};
constexpr std::array<std::uint32_t, 6> FailureColors{
    0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000, 0xFFFF00FF, 0xFF000000,
};

static_assert(__cplusplus >= 202002L, "The Vita memory probe requires C++20");
static_assert(sizeof(void*) == 4, "The Vita memory probe must be built for a 32-bit target");

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

/// Gives each of MemorySystem's large backing regions its own named, individually freeable
/// sceKernelAllocMemBlock instead of folding all of them into the newlib heap. This is the
/// hardware demonstration of Hito 4's "separate the allocations" requirement: it is deliberately
/// kept out of the deterministic corpus (budget_corpus.cpp), which needs a portable, heap-based
/// environment to stay comparable to the desktop reference bit for bit (see the heap comment
/// above) - this class only proves and measures the real allocation path, logged, never signed.
class MemblockEnvironment final : public Core::MemoryEnvironment {
public:
    bool IsPoweredOn() const override {
        return true;
    }
    VAddr GetRunningCorePC() const override {
        return 0;
    }
    std::shared_ptr<Kernel::Process> GetCurrentProcess() const override {
        return nullptr;
    }
    void FlushRegion(PAddr, u32) override {}
    void InvalidateRegion(PAddr, u32) override {}
    void FlushAndInvalidateRegion(PAddr, u32) override {}
    u32 ReadIoRegister32(VAddr) override {
        return 0;
    }
    void WriteIoRegister32(VAddr, u32) override {}
    void LogUnmappedAccess(Core::ExceptionType) override {}

    u8* AllocateBackingMemory(Memory::Region region, std::size_t size) override {
        const char* name = NameFor(region);
        const std::size_t aligned_size =
            (size + MemblockAlignment - 1) / MemblockAlignment * MemblockAlignment;
        const SceUID uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                                  static_cast<SceSize>(aligned_size), nullptr);
        if (uid < 0) {
            LOG_ERROR(Core_ARM11, "FAIL memory_block name={} bytes={} code=0x{:08X}", name, size,
                      static_cast<std::uint32_t>(uid));
            return nullptr;
        }
        void* base = nullptr;
        const int result = sceKernelGetMemBlockBase(uid, &base);
        if (result < 0 || base == nullptr) {
            LOG_ERROR(Core_ARM11, "FAIL memory_block_base name={} code=0x{:08X}", name,
                      static_cast<std::uint32_t>(result));
            sceKernelFreeMemBlock(uid);
            return nullptr;
        }
        // sceKernelAllocMemBlock does not zero-initialize; MemorySystem relies on FCRAM/VRAM/DSP
        // reading back as zero (see Core::MemoryEnvironment::AllocateBackingMemory's contract).
        std::memset(base, 0, size);
        uids[IndexFor(region)] = uid;
        LOG_INFO(Core_ARM11, "PASS memory_block name={} uid=0x{:08X} bytes={}", name,
                 static_cast<std::uint32_t>(uid), size);
        return static_cast<u8*>(base);
    }

    void FreeBackingMemory(Memory::Region region, u8* data, std::size_t) override {
        if (data == nullptr) {
            return;
        }
        SceUID& uid = uids[IndexFor(region)];
        if (uid >= 0) {
            sceKernelFreeMemBlock(uid);
            uid = -1;
        }
    }

private:
    static std::size_t IndexFor(Memory::Region region) {
        return static_cast<std::size_t>(region);
    }

    static const char* NameFor(Memory::Region region) {
        switch (region) {
        case Memory::Region::FCRAM:
            return "azahar-fcram";
        case Memory::Region::VRAM:
            return "azahar-vram";
        case Memory::Region::DSP:
            return "azahar-dsp";
        case Memory::Region::N3DS:
            return "azahar-n3ds";
        }
        return "azahar-unknown";
    }

    std::array<SceUID, 4> uids{-1, -1, -1, -1};
};

class Framebuffer {
public:
    int Allocate() {
        uid = sceKernelAllocMemBlock("azahar-vita-memory-framebuffer",
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
    LOG_INFO(Core_ARM11, "memory_probe_version={} platform=vita", ProbeVersion);

    Framebuffer framebuffer;
    int result = framebuffer.Allocate();
    if (result < 0) {
        return Fail(nullptr, "cdram_allocate", result);
    }
    framebuffer.DrawBands(SuccessColors);
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

#if defined(AZAHAR_VITA_BUDGET_FORCE_FAILURE)
    return Fail(&framebuffer, "forced_failure", static_cast<int>(0xA4000001U));
#endif

    // Hardware demonstration of the separated, named memblock allocation path (see
    // MemblockEnvironment above). Scoped so its ~134.5 MiB of memblocks are fully released before
    // the corpus below builds its own (heap-based) Memory::MemorySystem instances.
    {
        MemblockEnvironment environment;
        Memory::MemorySystem memory(environment);
        if (!memory.IsInitialized()) {
            const auto failed_item = memory.GetFailedItem();
            return Fail(&framebuffer, "memory_regions", failed_item ? static_cast<int>(*failed_item) : -1);
        }
        LOG_INFO(Core_ARM11,
                 "PASS memory_regions fcram={} vram={} dsp={} n3ds={} total={}",
                 memory.GetAllocatedBytes(Memory::Region::FCRAM),
                 memory.GetAllocatedBytes(Memory::Region::VRAM),
                 memory.GetAllocatedBytes(Memory::Region::DSP),
                 memory.GetAllocatedBytes(Memory::Region::N3DS), memory.GetTotalAllocatedBytes());
    }

    const Vita::BudgetProbe::BudgetReport report = Vita::BudgetProbe::RunBudgetCorpus(WorkDir);
    for (std::size_t i = 0; i < report.group_count; ++i) {
        const auto& group = report.groups[i];
        if (group.passed) {
            LOG_INFO(Core_ARM11, "PASS group={} signature=0x{:08X}", group.name, group.signature);
        } else {
            LOG_ERROR(Core_ARM11, "FAIL group={} signature=0x{:08X}", group.name, group.signature);
        }
    }
    if (!report.Passed()) {
        return Fail(&framebuffer, "budget_corpus", -1);
    }

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
