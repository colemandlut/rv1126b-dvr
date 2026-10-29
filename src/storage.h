#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace dvr {

struct Segment {
  uint64_t seq = 0;
  time_t start = 0;  // 录像开始的墙钟时间
  int duration_s = 0;
  std::string path;
  uint64_t size = 0;
  bool event = false;
};

// 录像目录布局：
//   <root>/.tmp/    正在写的分段（GStreamer 直接写这里）
//   <root>/normal/  普通循环录像，空间不足时从最旧的删
//   <root>/event/   事件锁存录像，不参与普通循环覆盖，只受 event_max_percent 限制
// 文件名：<序号8位>_<YYYYmmdd_HHMMSS>_<通道>.<扩展名>
// 排序和删除按序号而不是时间，避免没有 RTC 时开机时间错乱导致误删。
class Storage {
 public:
  virtual ~Storage() = default;

  bool init(const std::string& root, const std::string& channel, const std::string& ext);
  const std::string& root() const { return root_; }
  std::string tmp_dir() const { return root_ + "/.tmp"; }

  // 分段写完后移入 normal/，返回新的分段信息
  bool commit(const std::string& tmp_path, time_t start, int duration_s, Segment& out);
  // 把 normal/ 下的分段移入 event/
  bool lock(Segment& seg);
  // 启动时把上次断电遗留在 .tmp 的分段收进 normal/（用文件修改时间推算开始时间）
  int recover_tmp(int segment_seconds);
  // 删除最旧的录像直到空间满足要求；返回删除的文件数
  int enforce_space(uint64_t min_free_bytes, int event_max_percent);

  std::vector<Segment> list(bool event) const;
  uint64_t next_seq() const { return next_seq_; }

  static std::string make_name(uint64_t seq, time_t start, const std::string& channel, const std::string& ext);
  static bool parse_name(const std::string& name, uint64_t& seq, time_t& start);

 protected:
  // 测试中可覆盖
  virtual uint64_t free_bytes() const;
  virtual uint64_t total_bytes() const;

 private:
  std::string root_;
  std::string channel_;
  std::string ext_;
  uint64_t next_seq_ = 1;
};

bool mkdirs(const std::string& path);
bool is_mounted(const std::string& mountpoint);

}  // namespace dvr
