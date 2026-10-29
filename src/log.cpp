#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sys/stat.h>

namespace dvr {
namespace {

std::mutex g_mu;
std::string g_path;
size_t g_max_bytes = 1 << 20;
int g_keep = 3;
LogLevel g_min = LogLevel::Info;
FILE* g_file = nullptr;

const char* level_name(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return "D";
    case LogLevel::Info: return "I";
    case LogLevel::Warn: return "W";
    default: return "E";
  }
}

void rotate_locked() {
  if (g_file) {
    fclose(g_file);
    g_file = nullptr;
  }
  for (int i = g_keep - 1; i >= 1; --i) {
    std::string from = g_path + "." + std::to_string(i);
    std::string to = g_path + "." + std::to_string(i + 1);
    rename(from.c_str(), to.c_str());
  }
  rename(g_path.c_str(), (g_path + ".1").c_str());
  g_file = fopen(g_path.c_str(), "a");
}

}  // namespace

void log_init(const std::string& path, size_t max_bytes, int keep_files, LogLevel min_level) {
  std::lock_guard<std::mutex> lk(g_mu);
  g_path = path;
  g_max_bytes = max_bytes;
  g_keep = keep_files < 1 ? 1 : keep_files;
  g_min = min_level;
  if (g_file) fclose(g_file);
  g_file = path.empty() ? nullptr : fopen(path.c_str(), "a");
}

void log_write(LogLevel level, const char* fmt, ...) {
  if (level < g_min) return;
  char msg[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  char ts[32];
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

  std::lock_guard<std::mutex> lk(g_mu);
  fprintf(stderr, "%s %s %s\n", ts, level_name(level), msg);
  if (!g_file) return;
  fprintf(g_file, "%s %s %s\n", ts, level_name(level), msg);
  fflush(g_file);
  struct stat st;
  if (fstat(fileno(g_file), &st) == 0 && static_cast<size_t>(st.st_size) > g_max_bytes) rotate_locked();
}

}  // namespace dvr
