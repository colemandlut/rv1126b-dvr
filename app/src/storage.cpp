#include "storage.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "log.h"

namespace dvr {

bool mkdirs(const std::string& path) {
  if (path.empty()) return false;
  std::string cur;
  size_t pos = 0;
  while (pos != std::string::npos) {
    pos = path.find('/', pos + 1);
    cur = path.substr(0, pos);
    if (cur.empty()) continue;
    if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
  }
  return true;
}

bool is_mounted(const std::string& mountpoint) {
  std::ifstream in("/proc/mounts");
  std::string dev, mnt, rest;
  while (in >> dev >> mnt) {
    std::getline(in, rest);
    if (mnt == mountpoint) return true;
  }
  return false;
}

namespace {

void sync_dir(const std::string& dir) {
  int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    fsync(fd);
    close(fd);
  }
}

std::vector<std::string> list_files(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    out.emplace_back(e->d_name);
  }
  closedir(d);
  return out;
}

uint64_t file_size(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
}

}  // namespace

std::string Storage::make_name(uint64_t seq, time_t start, const std::string& channel, const std::string& ext) {
  struct tm tmv;
  localtime_r(&start, &tmv);
  char ts[32];
  strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);
  char buf[128];
  snprintf(buf, sizeof(buf), "%08llu_%s_%s.%s", static_cast<unsigned long long>(seq), ts, channel.c_str(),
           ext.c_str());
  return buf;
}

bool Storage::parse_name(const std::string& name, uint64_t& seq, time_t& start) {
  unsigned long long s = 0;
  int Y, M, D, h, m, sec;
  if (sscanf(name.c_str(), "%8llu_%4d%2d%2d_%2d%2d%2d_", &s, &Y, &M, &D, &h, &m, &sec) != 7) return false;
  struct tm tmv {};
  tmv.tm_year = Y - 1900;
  tmv.tm_mon = M - 1;
  tmv.tm_mday = D;
  tmv.tm_hour = h;
  tmv.tm_min = m;
  tmv.tm_sec = sec;
  tmv.tm_isdst = -1;
  seq = s;
  start = mktime(&tmv);
  return true;
}

bool Storage::init(const std::string& root, const std::string& channel, const std::string& ext) {
  root_ = root;
  channel_ = channel;
  ext_ = ext;
  for (const char* sub : {"/.tmp", "/normal", "/event"}) {
    if (!mkdirs(root_ + sub)) {
      LOGE("创建目录失败 %s%s: %s", root_.c_str(), sub, strerror(errno));
      return false;
    }
  }
  next_seq_ = 1;
  for (bool ev : {false, true}) {
    for (const auto& s : list(ev)) next_seq_ = std::max(next_seq_, s.seq + 1);
  }
  return true;
}

std::vector<Segment> Storage::list(bool event) const {
  std::vector<Segment> out;
  std::string dir = root_ + (event ? "/event" : "/normal");
  for (const auto& name : list_files(dir)) {
    Segment s;
    if (!parse_name(name, s.seq, s.start)) continue;
    s.path = dir + "/" + name;
    s.size = file_size(s.path);
    s.event = event;
    out.push_back(s);
  }
  std::sort(out.begin(), out.end(), [](const Segment& a, const Segment& b) { return a.seq < b.seq; });
  return out;
}

void Storage::mark_open(const std::string& tmp_path, time_t start) {
  FILE* f = fopen((tmp_path + ".start").c_str(), "w");
  if (!f) return;
  fprintf(f, "%lld\n", static_cast<long long>(start));
  fclose(f);
}

bool Storage::commit(const std::string& tmp_path, time_t start, int duration_s, Segment& out) {
  out.seq = next_seq_++;
  out.start = start;
  out.duration_s = duration_s;
  out.event = false;
  out.path = root_ + "/normal/" + make_name(out.seq, start, channel_, ext_);
  if (rename(tmp_path.c_str(), out.path.c_str()) != 0) {
    LOGE("移动分段失败 %s -> %s: %s", tmp_path.c_str(), out.path.c_str(), strerror(errno));
    return false;
  }
  out.size = file_size(out.path);
  unlink((tmp_path + ".start").c_str());
  sync_dir(root_ + "/normal");
  return true;
}

bool Storage::lock(Segment& seg) {
  if (seg.event) return true;
  std::string name = seg.path.substr(seg.path.rfind('/') + 1);
  std::string dst = root_ + "/event/" + name;
  if (rename(seg.path.c_str(), dst.c_str()) != 0) {
    LOGE("锁存分段失败 %s: %s", seg.path.c_str(), strerror(errno));
    return false;
  }
  seg.path = dst;
  seg.event = true;
  sync_dir(root_ + "/event");
  LOGI("事件锁存 %s", name.c_str());
  return true;
}

int Storage::recover_tmp(int segment_seconds) {
  int n = 0;
  auto names = list_files(tmp_dir());
  std::sort(names.begin(), names.end());
  for (const auto& name : names) {
    std::string p = tmp_dir() + "/" + name;
    if (name.size() > 6 && name.compare(name.size() - 6, 6, ".start") == 0) {
      // 对应的分段文件不在了就清掉记录
      if (access(p.substr(0, p.size() - 6).c_str(), F_OK) != 0) unlink(p.c_str());
      continue;
    }
    struct stat st;
    if (stat(p.c_str(), &st) != 0) continue;
    if (st.st_size == 0) {
      unlink(p.c_str());
      unlink((p + ".start").c_str());
      continue;
    }
    time_t start = st.st_mtime - segment_seconds;
    long long recorded = 0;
    if (FILE* f = fopen((p + ".start").c_str(), "r")) {
      if (fscanf(f, "%lld", &recorded) == 1 && recorded > 0) start = static_cast<time_t>(recorded);
      fclose(f);
    }
    Segment seg;
    if (commit(p, start, static_cast<int>(st.st_mtime - start), seg)) {
      LOGW("收回上次未正常结束的分段 %s (%llu 字节)", seg.path.c_str(), static_cast<unsigned long long>(seg.size));
      ++n;
    }
  }
  return n;
}

uint64_t Storage::free_bytes() const {
  struct statvfs vs;
  if (statvfs(root_.c_str(), &vs) != 0) return 0;
  return static_cast<uint64_t>(vs.f_bavail) * vs.f_frsize;
}

uint64_t Storage::total_bytes() const {
  struct statvfs vs;
  if (statvfs(root_.c_str(), &vs) != 0) return 0;
  return static_cast<uint64_t>(vs.f_blocks) * vs.f_frsize;
}

int Storage::enforce_space(uint64_t min_free_bytes, int event_max_percent) {
  int deleted = 0;
  auto remove_seg = [&](const Segment& s) {
    if (unlink(s.path.c_str()) == 0) {
      ++deleted;
      LOGI("循环覆盖：删除%s录像 %s", s.event ? "事件" : "", s.path.c_str());
      return true;
    }
    LOGE("删除失败 %s: %s", s.path.c_str(), strerror(errno));
    return false;
  };

  // 1. 事件录像超出配额时删最旧的事件录像
  auto events = list(true);
  uint64_t event_bytes = 0;
  for (const auto& s : events) event_bytes += s.size;
  const uint64_t quota = total_bytes() / 100 * static_cast<uint64_t>(event_max_percent);
  for (const auto& s : events) {
    if (event_bytes <= quota) break;
    if (remove_seg(s)) event_bytes -= s.size;
  }

  // 2. 剩余空间不足时删最旧的普通录像
  auto normal = list(false);
  size_t i = 0;
  while (free_bytes() < min_free_bytes && i < normal.size()) remove_seg(normal[i++]);

  // 3. 普通录像删光了仍不够（事件录像占满），只能删最旧的事件录像
  if (free_bytes() < min_free_bytes) {
    events = list(true);
    size_t k = 0;
    while (free_bytes() < min_free_bytes && k < events.size()) remove_seg(events[k++]);
    if (free_bytes() < min_free_bytes) LOGE("清理后空间仍不足，可能存储卡上有其他文件");
  }
  return deleted;
}

}  // namespace dvr
