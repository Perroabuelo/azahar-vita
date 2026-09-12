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

/// Runs the Hito 4 memory budget corpus: the declared byte budget for every large allocation the
/// Vita port makes (budget_plan), a real Memory::MemorySystem's actual per-region accounting
/// against that plan (region_accounting), a deliberately-failed allocation recovered from cleanly
/// (oom_recovery), three independent construct/run/destroy cycles of the Hito 3 system corpus
/// producing byte-identical results (load_release_cycles), and a check that the plan still leaves
/// room for Hito 5's renderer (renderer_headroom). Shared verbatim between the desktop reference
/// and the Vita probe; see vita/src/system_corpus.h's RunSystemCycle for why `work_dir` exists.
BudgetReport RunBudgetCorpus(const std::string& work_dir = "");

} // namespace Vita::BudgetProbe
