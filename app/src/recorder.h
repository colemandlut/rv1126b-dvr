#pragma once
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "gpio.h"
#include "pipeline.h"
#include "storage.h"
#include "watchdog.h"

namespace dvr {

// 事件锁存窗口管理：事件发生在 t，则 [t-pre, t+post] 内的分段都要锁存
class EventLocker {
 public:
  void add(time_t t, int pre_s, int post_s);
  // 分段 [start, end] 与任一事件窗口重叠则返回 true
  bool overlaps(time_t start, time_t end) const;
  // 去掉在 now 之前已经结束的窗口
  void prune(time_t now);
  size_t pending() const { return windows_.size(); }

 private:
  struct Window {
    time_t begin, end;
  };
  std::vector<Window> windows_;
};

class Recorder {
 public:
  explicit Recorder(const Config& cfg) : cfg_(cfg) {}
  // 主循环，直到收到退出信号；返回进程退出码
  int run();

  // 以下供主循环和测试调用
  void handle_line(const std::string& line);
  void trigger_event();

 private:
  enum class State { Idle, Recording, PowerFail, Stopping };

  bool select_storage();
  bool start_pipeline();
  void stop_pipeline(int timeout_ms);
  void commit_open_segments();
  void on_segment_closed(const std::string& tmp_path);
  void handle_command(const std::string& cmd);
  void on_power_fail();
  void tick();
  void write_status();
  bool restart_budget_ok();

  Config cfg_;
  Storage storage_;
  Pipeline pipe_;
  GpioInput power_gpio_;
  Watchdog wd_;
  EventLocker events_;
  State state_ = State::Idle;
  std::string active_root_;
  std::string ext_ = "mp4";
  std::map<std::string, time_t> open_;  // .tmp 路径 -> 开始时间
  std::deque<Segment> recent_;          // 最近几个已封口的分段，用于事件预录
  std::vector<double> restarts_;        // 管道重启时刻（单调时钟秒）
  double started_at_ = 0;
  double last_closed_at_ = 0;
  double power_ok_since_ = 0;
  double next_restart_at_ = 0;
  bool wd_hold_ = false;  // 为 true 时停止喂狗，让硬件复位
  int ctl_fd_ = -1;
  std::string ctl_buf_;
  uint64_t segments_written_ = 0;
  uint64_t events_locked_ = 0;
};

double mono_now();

}  // namespace dvr
