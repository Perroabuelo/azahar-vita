// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "budget_corpus.h"

#include <cstddef>
#include "core/memory.h"
#include "core/memory_environment.h"
#include "system_corpus.h"

namespace Vita::BudgetProbe {
namespace {

u32 Mix(u32 signature, u32 value) {
    return (signature ^ value) * 16777619U;
}

/// A minimal, silent Core::MemoryEnvironment: enough to construct a Memory::MemorySystem without
/// Core::System, a running CPU, or a rasterizer. Used by every group below except
/// load_release_cycles, which reuses the Hito 3 corpus's own environment via RunSystemCycle.
class PlainEnvironment : public Core::MemoryEnvironment {
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
};

/// Refuses to allocate one chosen region, so oom_recovery can exercise
/// Memory::MemorySystem::IsInitialized()/GetFailedItem() without depending on any real host limit.
class FailingEnvironment final : public PlainEnvironment {
public:
    explicit FailingEnvironment(Memory::Region region_to_fail_) : region_to_fail(region_to_fail_) {}

    u8* AllocateBackingMemory(Memory::Region region, std::size_t size) override {
        if (region == region_to_fail) {
            return nullptr;
        }
        return PlainEnvironment::AllocateBackingMemory(region, size);
    }

private:
    Memory::Region region_to_fail;
};

/// The Vita's four large emulated-memory allocations plus the surrounding budget, as constants
/// declared here rather than read from the actual build. This matters because the desktop
/// reference (x86-64) and the Vita probe (ARMv7) do not share a pointer width or a DynCom
/// translation cache size, so any value derived from either would silently diverge between the
/// two builds. These are simply facts about what the Vita port is choosing to request, restated
/// identically on both sides - the same role FCRAM_SIZE/VRAM_SIZE/etc. already play for the
/// emulated regions themselves.
struct BudgetPlan {
    u64 fcram_bytes;
    u64 vram_bytes;
    u64 dsp_bytes;
    u64 n3ds_bytes;
    u64 page_table_bytes;
    u64 translation_cache_bytes;
    u64 framebuffer_bytes;
    u64 newlib_heap_bytes;
};

// Measured Vita user-RAM pool: _newlib_heap_size_user (192 MiB) + user_free_before (55.0 MiB) from
// the retained Hito 3 evidence (build-vita/evidence/hito-3/boot-normal.log).
constexpr u64 UserBudgetBytes = 247ULL * 1024 * 1024;
// Headroom this milestone reserves so Hito 5's software renderer has somewhere to start from.
constexpr u64 RendererReserveBytes = 32ULL * 1024 * 1024;

constexpr BudgetPlan MakeBudgetPlan() {
    return BudgetPlan{
        .fcram_bytes = Memory::FCRAM_SIZE, // Old 3DS FCRAM; the only mode the Vita port supports
        .vram_bytes = Memory::VRAM_SIZE,
        .dsp_bytes = Memory::DSP_RAM_SIZE,
        .n3ds_bytes = 0, // not allocated under AZAHAR_VITA - see core/memory.cpp
        // Memory::PageTable's nominal size on the Vita's own 32-bit ARMv7 target: one pointer (4
        // bytes) plus one PageType byte per entry. Deliberately not sizeof(Memory::PageTable): this
        // corpus's desktop reference is x86-64, where pointers are 8 bytes, so sizeof() would
        // silently disagree between the two builds even though both define AZAHAR_VITA=1.
        .page_table_bytes = static_cast<u64>(Memory::PAGE_TABLE_NUM_ENTRIES) * 4 +
                             static_cast<u64>(Memory::PAGE_TABLE_NUM_ENTRIES) *
                                 sizeof(Memory::PageType),
        .translation_cache_bytes = 2ULL * 1024 * 1024, // vita/CMakeLists.txt's TRANS_CACHE_SIZE
        .framebuffer_bytes = 2ULL * 1024 * 1024,        // 960x544x4 rounded to a 256 KiB CDRAM block
        .newlib_heap_bytes = 32ULL * 1024 * 1024,
    };
}

constexpr u64 TotalPlanBytes(const BudgetPlan& plan) {
    return plan.fcram_bytes + plan.vram_bytes + plan.dsp_bytes + plan.n3ds_bytes +
           plan.page_table_bytes + plan.translation_cache_bytes + plan.framebuffer_bytes +
           plan.newlib_heap_bytes;
}

} // namespace

GroupResult RunBudgetPlanGroup() {
    constexpr BudgetPlan plan = MakeBudgetPlan();
    constexpr u64 total = TotalPlanBytes(plan);
    u32 signature = 2166136261U;
    signature = Mix(signature, static_cast<u32>(plan.fcram_bytes));
    signature = Mix(signature, static_cast<u32>(plan.vram_bytes));
    signature = Mix(signature, static_cast<u32>(plan.dsp_bytes));
    signature = Mix(signature, static_cast<u32>(plan.n3ds_bytes));
    signature = Mix(signature, static_cast<u32>(plan.page_table_bytes));
    signature = Mix(signature, static_cast<u32>(plan.translation_cache_bytes));
    signature = Mix(signature, static_cast<u32>(plan.framebuffer_bytes));
    signature = Mix(signature, static_cast<u32>(plan.newlib_heap_bytes));
    const bool passed = total <= UserBudgetBytes;
    return {"budget_plan", passed, signature};
}

GroupResult RunRegionAccountingGroup() {
    PlainEnvironment environment;
    Memory::MemorySystem memory(environment);

    const bool initialized = memory.IsInitialized();
    const bool fcram_ok = memory.GetAllocatedBytes(Memory::Region::FCRAM) == Memory::FCRAM_SIZE;
    const bool vram_ok = memory.GetAllocatedBytes(Memory::Region::VRAM) == Memory::VRAM_SIZE;
    const bool dsp_ok = memory.GetAllocatedBytes(Memory::Region::DSP) == Memory::DSP_RAM_SIZE;
    const bool n3ds_ok = memory.GetAllocatedBytes(Memory::Region::N3DS) == 0;
    const u64 expected_total =
        static_cast<u64>(Memory::FCRAM_SIZE) + Memory::VRAM_SIZE + Memory::DSP_RAM_SIZE;
    const bool total_ok = memory.GetTotalAllocatedBytes() == expected_total;

    const bool passed = initialized && fcram_ok && vram_ok && dsp_ok && n3ds_ok && total_ok;
    u32 signature = 2166136261U;
    signature = Mix(signature, initialized ? 1 : 0);
    signature = Mix(signature, fcram_ok ? 1 : 0);
    signature = Mix(signature, vram_ok ? 1 : 0);
    signature = Mix(signature, dsp_ok ? 1 : 0);
    signature = Mix(signature, n3ds_ok ? 1 : 0);
    signature = Mix(signature, total_ok ? 1 : 0);
    return {"region_accounting", passed, signature};
}

GroupResult RunOomRecoveryGroup() {
    bool failed_as_expected = false;
    bool failed_item_is_fcram = false;
    {
        // Simulates the platform refusing the single largest request (128 MiB FCRAM). MemorySystem
        // must finish constructing anyway - see IsInitialized()'s doc comment in core/memory.h -
        // and both it and the environment are destroyed cleanly at the end of this scope, with
        // nothing left to crash or leak.
        FailingEnvironment environment(Memory::Region::FCRAM);
        Memory::MemorySystem memory(environment);
        failed_as_expected = !memory.IsInitialized();
        const auto failed_item = memory.GetFailedItem();
        failed_item_is_fcram = failed_item.has_value() && *failed_item == Memory::Region::FCRAM;
    }

    bool retry_succeeded = false;
    {
        // The "recovered from" half of the acceptance criterion: a fresh, unrestricted environment
        // must still succeed right after the failure above.
        PlainEnvironment environment;
        Memory::MemorySystem memory(environment);
        retry_succeeded = memory.IsInitialized();
    }

    const bool passed = failed_as_expected && failed_item_is_fcram && retry_succeeded;
    u32 signature = 2166136261U;
    signature = Mix(signature, failed_as_expected ? 1 : 0);
    signature = Mix(signature, failed_item_is_fcram ? 1 : 0);
    signature = Mix(signature, retry_succeeded ? 1 : 0);
    return {"oom_recovery", passed, signature};
}

GroupResult RunLoadReleaseCyclesGroup(const std::string& work_dir, CycleObserver observer) {
    // Two cycles is the minimum that can prove "repeating this doesn't change the outcome" - a
    // third cycle would only be useful to rule out a warm-up effect, which doesn't apply here since
    // each cycle builds a brand new Memory::MemorySystem/Kernel::KernelSystem from scratch with no
    // state carried over (see RunSystemCycle). Kept at 2 rather than 3 because each cycle repeats
    // the entire Hito 3 sequence, measured on real hardware at ~2.85 s (build-vita/evidence/hito-3),
    // dominated by zeroing the ~134.5 MiB it allocates - a real cost worth not paying a third time
    // for no additional signal.
    constexpr int NumCycles = 2;
    bool all_cycles_passed = true;
    u32 first_cycle_signature = 0;
    bool cycles_match = true;

    for (int i = 0; i < NumCycles; ++i) {
        // Each call independently constructs and destroys its own Memory::MemorySystem,
        // Kernel::KernelSystem and Core::ARM_DynCom (see system_corpus.cpp) - there is no shared
        // state a leak could hide in between iterations. What this proves is that independent
        // construct -> load -> run -> destroy cycles over the same inputs keep producing the exact
        // same outcome; the actual host memory accounting across cycles
        // (mallinfo/sceKernelGetFreeMemorySize) is measured and logged separately on Vita, since a
        // desktop x86-64 reference and an ARMv7 Vita build were never going to agree on that bit
        // for bit.
        const Vita::SystemProbe::SystemReport cycle = Vita::SystemProbe::RunSystemCycle(work_dir);
        const bool cycle_passed = cycle.Passed();
        all_cycles_passed = all_cycles_passed && cycle_passed;

        u32 cycle_signature = 2166136261U;
        for (std::size_t j = 0; j < cycle.group_count; ++j) {
            cycle_signature = Mix(cycle_signature, cycle.groups[j].signature);
        }
        if (i == 0) {
            first_cycle_signature = cycle_signature;
        } else if (cycle_signature != first_cycle_signature) {
            cycles_match = false;
        }

        if (observer != nullptr) {
            observer(i);
        }
    }

    const bool passed = all_cycles_passed && cycles_match;
    const u32 signature =
        Mix(Mix(first_cycle_signature, all_cycles_passed ? 1 : 0), cycles_match ? 1 : 0);
    return {"load_release_cycles", passed, signature};
}

GroupResult RunRendererHeadroomGroup() {
    constexpr BudgetPlan plan = MakeBudgetPlan();
    constexpr u64 total = TotalPlanBytes(plan);
    const bool passed = (total + RendererReserveBytes) <= UserBudgetBytes;
    return {"renderer_headroom", passed, Mix(2166136261U, passed ? 1 : 0)};
}

bool BudgetReport::Passed() const {
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

BudgetReport RunBudgetCorpus(const std::string& work_dir) {
    BudgetReport report{};
    report.groups[report.group_count++] = RunBudgetPlanGroup();
    report.groups[report.group_count++] = RunRegionAccountingGroup();
    report.groups[report.group_count++] = RunOomRecoveryGroup();
    report.groups[report.group_count++] = RunLoadReleaseCyclesGroup(work_dir);
    report.groups[report.group_count++] = RunRendererHeadroomGroup();
    return report;
}

} // namespace Vita::BudgetProbe
