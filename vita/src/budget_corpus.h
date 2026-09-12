// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include "common/common_types.h"

namespace Vita::BudgetProbe {

struct GroupResult {
    const char* name{};
    bool passed{};
    u32 signature{};
};

struct BudgetReport {
    std::array<GroupResult, 5> groups{};
    std::size_t group_count{};

    [[nodiscard]] bool Passed() const;
};

/// Invoked after each construct/run/destroy cycle inside RunLoadReleaseCyclesGroup, with the
/// 0-based cycle index. A single cycle repeats the whole Hito 3 loader/memory/kernel/CPU sequence
/// and is measurably slow on real hardware (the retained Hito 3 evidence shows one such cycle
/// taking ~2.85 s, dominated by zeroing the ~134.5 MiB it allocates) with nothing else to show for
/// it on screen, so the Vita probe uses this purely to log progress; the desktop reference and
/// RunBudgetCorpus's default both pass nullptr, meaning "no observer".
using CycleObserver = void (*)(int cycle_index);

/// The declared byte budget for every large allocation the Vita port makes.
GroupResult RunBudgetPlanGroup();
/// A real Memory::MemorySystem's actual per-region accounting, checked against that plan.
GroupResult RunRegionAccountingGroup();
/// A deliberately-failed allocation recovered from cleanly.
GroupResult RunOomRecoveryGroup();
/// Independent construct/run/destroy cycles of the Hito 3 system corpus (see system_corpus.h's
/// RunSystemCycle), checked for byte-identical results across all cycles.
GroupResult RunLoadReleaseCyclesGroup(const std::string& work_dir, CycleObserver observer = nullptr);
/// Checks the plan still leaves room for Hito 5's renderer.
GroupResult RunRendererHeadroomGroup();

/// Runs all five groups above in order and collects them into one report. Shared verbatim between
/// the desktop reference and the Vita probe; see vita/src/system_corpus.h's RunSystemCycle for why
/// `work_dir` exists. The Vita probe calls the group functions above individually instead (with a
/// CycleObserver and progress logging between groups - see budget_main.cpp) rather than this
/// aggregate, precisely because of how slow load_release_cycles is on hardware; this function stays
/// for the desktop reference, where none of that matters.
BudgetReport RunBudgetCorpus(const std::string& work_dir = "");

} // namespace Vita::BudgetProbe
