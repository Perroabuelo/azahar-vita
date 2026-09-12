// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdio>
#include "dyncom_corpus.h"

int main() {
    const Vita::DyncomProbe::CorpusReport report = Vita::DyncomProbe::RunCorpus();
    const Vita::DyncomProbe::BenchmarkResult benchmark = Vita::DyncomProbe::RunBenchmark(10000);
    std::printf("dyncom_probe_version=02.00 platform=desktop\n");
    for (const auto& group : report.groups) {
        std::printf("%s group=%s signature=0x%08X instructions=%llu\n",
                    group.passed ? "PASS" : "FAIL", group.name, group.signature,
                    static_cast<unsigned long long>(group.instructions));
    }
    std::printf("%s memory bounds=%s\n", report.invalid_access ? "FAIL" : "PASS",
                report.invalid_access ? "invalid-access" : "clean");
    std::printf("%s benchmark instructions=%llu final=%u\n",
                benchmark.passed ? "PASS" : "FAIL",
                static_cast<unsigned long long>(benchmark.instructions), benchmark.final_value);
    const bool passed = report.Passed() && benchmark.passed;
    std::printf("RESULT %s instructions=%llu\n", passed ? "PASS" : "FAIL",
                static_cast<unsigned long long>(report.instructions));
    return passed ? 0 : 1;
}
