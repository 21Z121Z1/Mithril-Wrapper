#include "util/log.h"

#include <cstdarg>
#include <cstdio>

namespace mithril {
namespace util {

namespace {
int g_min_level = 2; // Info
}

void Log(LogLevel level, const char* fmt, ...) {
    if (static_cast<int>(level) > g_min_level)
        return;

    static const char* kPrefix[] = {"[MR-ERR] ", "[MR-WARN]", "[MR-INFO]", "[MR-DBG ]"};
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s %s\n", kPrefix[static_cast<int>(level)], buf);
}

} // namespace util
} // namespace mithril