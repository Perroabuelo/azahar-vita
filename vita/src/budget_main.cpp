// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
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

// See system_main.cpp for the newlib heap background. Hito 3 needed 192 MiB because FCRAM, VRAM,
// DSP RAM and 4 MiB of New 3DS extra RAM all landed in this same heap. Hito 4 removes the N3DS
// allocation entirely (see core/memory.cpp's N3DS_EXTRA_RAM_ALLOCATED_SIZE), but the deterministic
// corpus shared with the desktop reference (budget_corpus.cpp) still constructs its own
// Memory::MemorySystem instances through the portable, heap-based default environment, exactly as
// the Hito 2/3 corpora do, so its results stay comparable to the desktop build bit for bit. That
// means this probe's peak heap need is still close to a full ~134.5 MiB (FCRAM+VRAM+DSP) plus the
// ~5 MiB page table and kernel object overhead a loaded homebrew adds.
//
// _newlib_heap_size_user reserves this size as ONE sceKernelAllocMemBlock the moment the process
// starts, for its entire lifetime, whether or not anything has actually been malloc'd from it yet
// - it is not a lazily-grown limit. An earlier version of this probe additionally built a second,
// separate Memory::MemorySystem over a real sceKernelAllocMemBlock-backed environment (to
// demonstrate the "separate the allocations" requirement live), on the mistaken assumption that
// its ~134.5 MiB request would not overlap the heap's own reservation. It does: hardware testing
// on 2026-09-12 found that a 160 MiB heap plus that demonstration's extra ~134.5 MiB request
// exceeded the Vita's ~247 MiB user-RAM pool, and the single largest request (128 MiB FCRAM) was
// the one the kernel refused (SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE, 0x80024302) - caught cleanly
// by the same IsInitialized()/GetFailedItem() seam this milestone added, not a crash, but wrong
// nonetheless. The demonstration was removed rather than shrinking the heap further, since the
// corpus's own heap-based Memory::MemorySystem instances (region_accounting, oom_recovery,
// load_release_cycles) already need close to this much heap by themselves; running the real
// sceKernelAllocMemBlock path for FCRAM/VRAM/DSP *instead of* the corpus's heap-based one (so the
// two are never both reserved at once) is future work, same as the follow-up already noted for
// bringing the corpus's own footprint down (see docs/vita-port.md).
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

// A single load_release_cycles cycle repeats the whole Hito 3 sequence and is measurably slow on
// real hardware (~2.85 s in the retained Hito 3 evidence, dominated by zeroing ~134.5 MiB), with
// nothing to show for it on screen in the meantime. This gives that group somewhere to report
// progress to, so a run that is just slow is distinguishable in boot.log from one that is stuck:
// physical testing on 2026-09-12 found the probe unresponsive to START for long enough to look
// hung, and the recovered log had stopped at "PASS logger" with no further record at all.
void LogCycleProgress(int cycle_index) {
    LOG_INFO(Core_ARM11, "PASS memory_cycle_progress cycle={}", cycle_index);
}

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

    // Calls the same five groups RunBudgetCorpus (used by the desktop reference) runs, but one at a
    // time with a progress line logged immediately after each - see LogCycleProgress's comment for
    // why. Each LOG_INFO here is flushed to boot.log as soon as it's written (see
    // common/logging/backend_vita.cpp), so if a run ever again looks stuck, the recovered log shows
    // exactly how far it got instead of stopping at "PASS logger".
    Vita::BudgetProbe::BudgetReport report{};
    report.groups[report.group_count++] = Vita::BudgetProbe::RunBudgetPlanGroup();
    LOG_INFO(Core_ARM11, "PASS memory_progress group=budget_plan");
    report.groups[report.group_count++] = Vita::BudgetProbe::RunRegionAccountingGroup();
    LOG_INFO(Core_ARM11, "PASS memory_progress group=region_accounting");
    report.groups[report.group_count++] = Vita::BudgetProbe::RunOomRecoveryGroup();
    LOG_INFO(Core_ARM11, "PASS memory_progress group=oom_recovery");
    report.groups[report.group_count++] =
        Vita::BudgetProbe::RunLoadReleaseCyclesGroup(WorkDir, &LogCycleProgress);
    LOG_INFO(Core_ARM11, "PASS memory_progress group=load_release_cycles");
    report.groups[report.group_count++] = Vita::BudgetProbe::RunRendererHeadroomGroup();
    LOG_INFO(Core_ARM11, "PASS memory_progress group=renderer_headroom");

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
