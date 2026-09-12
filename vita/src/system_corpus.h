// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include "common/common_types.h"

namespace Vita::SystemProbe {

struct GroupResult {
    const char* name{};
    bool passed{};
    u32 signature{};
};

struct SystemReport {
    std::array<GroupResult, 5> groups{};
    std::size_t group_count{};

    [[nodiscard]] bool Passed() const;
};

/// Builds a deterministic, self-contained 3DSX homebrew image in memory. Its entry code writes a
/// known pattern to its own data segment, connects to the "probe:test" port, exchanges one IPC
/// request/response, emits a recognizable string via OutputDebugString, then calls ExitProcess.
/// No relocations are used: every address the code touches is an absolute constant already known
/// at build time (see vita/src/system_corpus.cpp for the exact layout).
std::vector<u8> BuildTestHomebrew();

/// Builds a deliberately truncated (header-only) image that Identify3DSXImage/Load3DSXImage must
/// reject, to exercise the loader's diagnostic path.
std::vector<u8> BuildCorruptHomebrew();

/// One full loader -> memory -> kernel -> entry point -> SVC/IPC -> exit cycle: builds its own
/// Memory::MemorySystem, Kernel::KernelSystem and Core::ARM_DynCom from scratch and destroys them
/// before returning. RunSystemCorpus (Hito 3) calls this once; Hito 4's budget corpus calls it
/// several times in a row to check that the same inputs keep producing byte-identical results
/// across independent construct/run/destroy cycles - a repeatable stand-in for "does not grow"
/// that doesn't depend on any single host's allocator internals (see budget_corpus.cpp).
SystemReport RunSystemCycle(const std::string& work_dir = "");

/// Runs one RunSystemCycle() and returns it verbatim. Kept as a separate name (rather than having
/// callers use RunSystemCycle directly) because "the Hito 3 system corpus" and "one repeatable
/// cycle of it" are different concepts to a reader, even though Hito 4 added no new behavior here.
SystemReport RunSystemCorpus(const std::string& work_dir = "");

} // namespace Vita::SystemProbe
