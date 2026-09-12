// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include "common/common_types.h"

namespace Vita::DyncomProbe {

struct GroupResult {
    const char* name{};
    bool passed{};
    u32 signature{};
    u64 instructions{};
};

struct CorpusReport {
    std::array<GroupResult, 7> groups{};
    std::size_t group_count{};
    bool invalid_access{};
    u64 instructions{};

    [[nodiscard]] bool Passed() const;
};

struct BenchmarkResult {
    bool passed{};
    u64 instructions{};
    u32 final_value{};
};

CorpusReport RunCorpus();
BenchmarkResult RunBenchmark(u32 iterations);

} // namespace Vita::DyncomProbe
