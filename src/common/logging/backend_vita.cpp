// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>

#include <fmt/format.h>

#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/logging/log_entry.h"
#include "common/logging/text_formatter.h"

namespace Common::Log {
namespace {

std::mutex log_mutex;
Filter global_filter;
std::FILE* log_file{};
bool logging_initialized{};
bool suppress_logging{true};
const auto time_origin = std::chrono::steady_clock::now();

Entry CreateEntry(Class log_class, Level log_level, const char* filename, unsigned int line_num,
                  const char* function, std::string message) {
    return {
        .timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - time_origin),
        .log_class = log_class,
        .log_level = log_level,
        .filename = filename,
        .line_num = line_num,
        .function = function,
        .message = std::move(message),
    };
}

void WriteEntry(const Entry& entry) {
    const std::string line = FormatLogMessage(entry).append(1, '\n');
    if (log_file != nullptr) {
        static_cast<void>(std::fwrite(line.data(), 1, line.size(), log_file));
        // The probe log is diagnostic evidence, so every complete record must survive a crash.
        static_cast<void>(std::fflush(log_file));
    } else {
        static_cast<void>(std::fwrite(line.data(), 1, line.size(), stderr));
        static_cast<void>(std::fflush(stderr));
    }
}

} // namespace

void Initialize(std::string_view filename) {
    std::scoped_lock lock(log_mutex);
    if (log_file != nullptr) {
        return;
    }

    const auto& log_dir = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
    static_cast<void>(FileUtil::CreateFullPath(log_dir));
    const std::string path = log_dir + std::string(filename.empty() ? LOG_FILE : filename);
    log_file = std::fopen(path.c_str(), "w");
    logging_initialized = log_file != nullptr;
    suppress_logging = false;
}

void Start() {}

void Stop() {
    std::scoped_lock lock(log_mutex);
    if (log_file != nullptr) {
        static_cast<void>(std::fflush(log_file));
        static_cast<void>(std::fclose(log_file));
        log_file = nullptr;
    }
    logging_initialized = false;
}

void DisableLoggingInTests() {
    suppress_logging = true;
}

void SetGlobalFilter(const Filter& filter) {
    std::scoped_lock lock(log_mutex);
    global_filter = filter;
}

bool SetRegexFilter(const std::string& regex) {
    // Regex filtering is intentionally excluded from the minimal Vita dependency graph.
    return regex.empty();
}

void SetColorConsoleBackendEnabled(bool) {}

void FmtLogMessageImpl(Class log_class, Level log_level, const char* filename,
                       unsigned int line_num, const char* function, fmt::string_view format,
                       const fmt::format_args& args) {
    if (suppress_logging && log_level < Level::Critical) {
        return;
    }

    Entry entry = CreateEntry(log_class, log_level, filename, line_num, function,
                              fmt::vformat(format, args));
    std::scoped_lock lock(log_mutex);
    if (logging_initialized && !global_filter.CheckMessage(log_class, log_level)) {
        return;
    }
    WriteEntry(entry);
}

} // namespace Common::Log
