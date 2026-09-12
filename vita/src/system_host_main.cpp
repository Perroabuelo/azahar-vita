// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdio>
#include "system_corpus.h"

int main() {
    const Vita::SystemProbe::SystemReport report = Vita::SystemProbe::RunSystemCorpus();
    std::printf("system_probe_version=00.01 platform=desktop\n");
    for (std::size_t i = 0; i < report.group_count; ++i) {
        const auto& group = report.groups[i];
        std::printf("%s group=%s signature=0x%08X\n", group.passed ? "PASS" : "FAIL", group.name,
                    group.signature);
    }
    const bool passed = report.Passed();
    std::printf("RESULT %s groups=%zu\n", passed ? "PASS" : "FAIL", report.group_count);
    return passed ? 0 : 1;
}
