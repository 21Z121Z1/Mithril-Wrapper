#pragma once

namespace mithril {
namespace util {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

void Log(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

#define ML_LOG_ERROR(...) ::mithril::util::Log(::mithril::util::LogLevel::Error, __VA_ARGS__)
#define ML_LOG_WARN(...)  ::mithril::util::Log(::mithril::util::LogLevel::Warn, __VA_ARGS__)
#define ML_LOG_INFO(...)  ::mithril::util::Log(::mithril::util::LogLevel::Info, __VA_ARGS__)
#define ML_LOG_DEBUG(...) ::mithril::util::Log(::mithril::util::LogLevel::Debug, __VA_ARGS__)

} // namespace util
} // namespace mithril
