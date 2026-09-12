// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdio>
#include "common/logging/backend.h"
#include "common/logging/log.h"

namespace Common::Log {

void FmtLogMessageImpl(Class, Level, const char*, unsigned int, const char*, fmt::string_view format,
                       const fmt::format_args& args) {
    fmt::vprint(stderr, format, args);
    std::fputc('\n', stderr);
}

void Stop() {}

} // namespace Common::Log
