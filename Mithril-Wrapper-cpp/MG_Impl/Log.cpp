// Mithril-Wrapper - MG_Impl/Log.cpp
// Logging implementation (stderr-based). Ported from the former gl/log.cpp.
#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

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

// iOS 启动器没有环境变量入口（JVM 参数也到不了 native 侧），
// 用「Documents/mithril_debug 文件夹存在」做开关：文件 App 能新建
// 文件夹（新建空文件反而做不到）。跨平台读 $HOME/Documents。
bool debug_marker_exists() {
    const char* home = std::getenv("HOME");
    if (!home) return false;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/Documents/mithril_debug", home);
    struct stat st{};
    return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
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
        else if (debug_marker_exists()) log_set_level(LogLevel::Verbose);
        else {
            // 零配置诊断默认值：Info（含 init 横幅 / swapchain 配置 / OOM
            // 尸检 / 恢复路径）。iOS 启动器没有环境变量入口，诊断必须
            // 开箱即用；Info 级不会逐帧刷屏。
            log_set_level(LogLevel::Info);
        }
    }
} g_log_init;
} // namespace

} // namespace mithril
