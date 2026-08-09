// Mithril-Wrapper - MG_Impl/Log.cpp
// Logging implementation (stderr-based). Ported from the former gl/log.cpp.
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace mithril {

namespace {
LogLevel g_level = LogLevel::Warning;

const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::Verbose: return "V";
        case LogLevel::Debug:   return "D";
        case LogLevel::Info:    return "I";
        case LogLevel::Warning: return "W";
        case LogLevel::Error:   return "E";
    }
    return "?";
}

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    return v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y');
}
} // namespace

void log_set_level(LogLevel level) { g_level = level; }
LogLevel log_get_level() { return g_level; }

void log_write(LogLevel level, const char* tag, const char* fmt, ...) {
    if (static_cast<int>(level) < static_cast<int>(g_level)) return;

    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    std::tm tm{};
    localtime_r(&ts.tv_sec, &tm);

    std::fprintf(stderr, "[mithril %s %02d:%02d:%02d.%03ld %s] ",
                 level_str(level), tm.tm_hour, tm.tm_min, tm.tm_sec,
                 ts.tv_nsec / 1000000, tag ? tag : "");

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);

    std::fputc('\n', stderr);
}

// Initialise level from environment on first use.
namespace {
struct LogLevelInit {
    LogLevelInit() {
        if (env_flag("MITHRIL_VERBOSE")) log_set_level(LogLevel::Verbose);
        else if (env_flag("MITHRIL_DEBUG")) log_set_level(LogLevel::Debug);
        else if (env_flag("MITHRIL_INFO")) log_set_level(LogLevel::Info);
    }
} g_log_init;
} // namespace

} // namespace mithril
