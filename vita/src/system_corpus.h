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

/// Runs the full loader -> memory -> kernel -> entry point -> SVC/IPC -> exit sequence against the
/// real Memory::MemorySystem, Kernel::KernelSystem and Loader::Load3DSXImage, driven by a real
/// Core::ARM_DynCom. Returns a report shared verbatim between the desktop reference and the Vita
/// probe. `work_dir` (with a trailing separator if non-empty) is where the synthetic 3DSX images
/// are written and then loaded back from; it defaults to the current directory, which is what the
/// desktop reference wants, while the Vita probe passes its ux0:data/azahar-vita/hito-3/ directory.
SystemReport RunSystemCorpus(const std::string& work_dir = "");

} // namespace Vita::SystemProbe
