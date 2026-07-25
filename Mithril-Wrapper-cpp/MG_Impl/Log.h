// Mithril-Wrapper - MG_Impl/Log.h
// Logging helpers (stderr-based; lightweight, no dependencies). Ported from
// the former gl/log.h so all translation units (MG_State, MG_Impl, MG_Backend)
// share one logging implementation.
#ifndef MITHRIL_LOG_H
#define MITHRIL_LOG_H

#include <cstdarg>

namespace mithril {

enum class LogLevel { Verbose = 0, Debug, Info, Warning, Error };

void log_set_level(LogLevel level);
LogLevel log_get_level();

void log_write(LogLevel level, const char* tag, const char* fmt, ...);

} // namespace mithril

#define MITHRIL_LOG_VERBOSE(tag, ...) ::mithril::log_write(::mithril::LogLevel::Verbose, tag, __VA_ARGS__)
#define MITHRIL_LOG_DEBUG(tag, ...)   ::mithril::log_write(::mithril::LogLevel::Debug,   tag, __VA_ARGS__)
#define MITHRIL_LOG_INFO(tag, ...)    ::mithril::log_write(::mithril::LogLevel::Info,    tag, __VA_ARGS__)
#define MITHRIL_LOG_WARN(tag, ...)    ::mithril::log_write(::mithril::LogLevel::Warning, tag, __VA_ARGS__)
#define MITHRIL_LOG_ERROR(tag, ...)   ::mithril::log_write(::mithril::LogLevel::Error,   tag, __VA_ARGS__)

#endif // MITHRIL_LOG_H
