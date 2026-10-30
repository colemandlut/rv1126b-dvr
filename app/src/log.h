#pragma once
#include <string>

namespace dvr {

enum class LogLevel { Debug, Info, Warn, Error };

// 简单的滚动日志：同时写 stderr 和文件，文件超过 max_bytes 后轮转为 .1 .2 ...
void log_init(const std::string& path, size_t max_bytes, int keep_files, LogLevel min_level);
void log_write(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace dvr

#define LOGD(...) ::dvr::log_write(::dvr::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::dvr::log_write(::dvr::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::dvr::log_write(::dvr::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::dvr::log_write(::dvr::LogLevel::Error, __VA_ARGS__)
