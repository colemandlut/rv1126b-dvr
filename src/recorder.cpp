#include "recorder.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

namespace dvr {

namespace {

volatile sig_atomic_t g_quit = 0;
volatile sig_atomic_t g_event = 0;

void on_signal(int sig) {
  if (sig == SIGUSR1)
    g_event = 1;
  else
    g_quit = 1;
}

constexpr int kRecentKeep = 4;
constexpr double kRestartDelay = 2.0;

}  // namespace

double mono_now() {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<double>(t.tv_sec) + t.tv_nsec / 1e9;
}

// ---------------- EventLocker ----------------

void EventLocker::add(time_t t, int pre_s, int post_s) { windows_.push_back({t - pre_s, t + post_s}); }

bool EventLocker::overlaps(time_t start, time_t end) const {
  for (const auto& w : windows_) {
    if (start < w.end && end > w.begin) return true;
  }
  return false;
}

void EventLocker::prune(time_t now) {
  std::vector<Window> keep;
  for (const auto& w : windows_) {
    if (w.end >= now) keep.push_back(w);
  }
  windows_.swap(keep);
}

// ---------------- Recorder ----------------

bool Recorder::select_storage() {
  const auto& s = cfg_.storage;
  std::string root = s.root;
  if (!s.require_mount.empty() && !is_mounted(s.require_mount)) {
    if (active_root_ != s.fallback_root) LOGW("%s 未挂载（没插 SD 卡？），临时录到 %s", s.require_mount.c_str(), s.fallback_root.c_str());
    root = s.fallback_root;
  }
  if (root == active_root_) return true;
  if (!storage_.init(root, s.channel, ext_)) return false;
  active_root_ = root;
  int n = storage_.recover_tmp(s.segment_seconds);
  LOGI("录像目录 %s，普通录像 %zu 段，事件录像 %zu 段，收回遗留分段 %d 个", root.c_str(), storage_.list(false).size(),
       storage_.list(true).size(), n);
  return true;
}

bool Recorder::start_pipeline() {
  const auto& v = cfg_.video;
  std::map<std::string, std::string> vars = {
      {"device", v.device},
      {"width", std::to_string(v.width)},
      {"height", std::to_string(v.height)},
      {"fps", std::to_string(v.fps)},
      {"bitrate", std::to_string(v.bitrate_kbps * 1000)},
      {"bitrate_kbps", std::to_string(v.bitrate_kbps)},
      {"gop", std::to_string(v.gop)},
      {"rotation", std::to_string(v.rotation)},
      {"segment_ns", std::to_string(static_cast<long long>(cfg_.storage.segment_seconds) * 1000000000LL)},
      {"location", storage_.tmp_dir() + "/seg%05d." + ext_},
  };
  std::string args = expand_template(v.pipeline, vars);
  if (!pipe_.start(args, [this](const std::string& l) { handle_line(l); })) {
    LOGE("启动 gst-launch-1.0 失败: %s", strerror(errno));
    return false;
  }
  started_at_ = mono_now();
  last_closed_at_ = 0;
  state_ = State::Recording;
  return true;
}

void Recorder::stop_pipeline(int timeout_ms) {
  pipe_.stop(timeout_ms);
  commit_open_segments();
  sync();
}

void Recorder::commit_open_segments() {
  // 管道退出后仍在 open_ 里的分段说明没收到 closed 消息（被强杀或掉电），直接收进 normal/
  auto pending = open_;
  for (const auto& kv : pending) {
    struct stat st;
    if (stat(kv.first.c_str(), &st) != 0) {
      open_.erase(kv.first);
      continue;
    }
    if (st.st_size == 0) {
      unlink(kv.first.c_str());
      open_.erase(kv.first);
      continue;
    }
    on_segment_closed(kv.first);
  }
}

void Recorder::handle_line(const std::string& line) {
  std::string kind, loc;
  if (!parse_split_message(line, kind, loc)) {
    if (line.find("ERROR") != std::string::npos || line.find("WARNING") != std::string::npos)
      LOGW("gst: %s", line.c_str());
    else
      LOGD("gst: %s", line.c_str());
    return;
  }
  if (kind == "opened") {
    open_[loc] = time(nullptr);
    LOGD("分段开始 %s", loc.c_str());
  } else {
    on_segment_closed(loc);
  }
}

void Recorder::on_segment_closed(const std::string& tmp_path) {
  time_t now = time(nullptr);
  auto it = open_.find(tmp_path);
  time_t start = it != open_.end() ? it->second : now - cfg_.storage.segment_seconds;
  if (it != open_.end()) open_.erase(it);

  Segment seg;
  if (!storage_.commit(tmp_path, start, static_cast<int>(now - start), seg)) return;
  ++segments_written_;
  last_closed_at_ = mono_now();
  LOGI("分段完成 %s（%llu KB，%d 秒）", seg.path.c_str(), static_cast<unsigned long long>(seg.size / 1024), seg.duration_s);

  if (events_.overlaps(seg.start, now) && storage_.lock(seg)) ++events_locked_;
  events_.prune(now);

  recent_.push_back(seg);
  while (recent_.size() > kRecentKeep) recent_.pop_front();

  storage_.enforce_space(cfg_.storage.min_free_mb * 1024 * 1024, cfg_.storage.event_max_percent);
}

void Recorder::trigger_event() {
  time_t now = time(nullptr);
  LOGI("收到事件，锁存前 %d 秒和后 %d 秒的录像", cfg_.event.pre_seconds, cfg_.event.post_seconds);
  events_.add(now, cfg_.event.pre_seconds, cfg_.event.post_seconds);
  // 已经封口的分段如果落在预录窗口内，立即锁存；正在写的分段在封口时处理
  for (auto& seg : recent_) {
    time_t end = seg.start + std::max(seg.duration_s, 1);
    if (!seg.event && events_.overlaps(seg.start, end) && storage_.lock(seg)) ++events_locked_;
  }
}

void Recorder::on_power_fail() {
  if (state_ == State::PowerFail) return;
  LOGW("检测到掉电，封口当前分段并同步");
  if (pipe_.running()) stop_pipeline(cfg_.power_fail.finalize_timeout_ms);
  sync();
  state_ = State::PowerFail;
  power_ok_since_ = 0;
  write_status();
}

void Recorder::handle_command(const std::string& cmd) {
  if (cmd == "event") {
    trigger_event();
  } else if (cmd == "powerfail") {  // 测试用：模拟掉电
    on_power_fail();
  } else if (cmd == "powerok") {
    if (state_ == State::PowerFail) power_ok_since_ = mono_now() - cfg_.power_fail.recover_ms / 1000.0;
  } else if (cmd == "restart") {
    stop_pipeline(cfg_.power_fail.finalize_timeout_ms);
    state_ = State::Idle;
    next_restart_at_ = 0;
  } else if (!cmd.empty()) {
    LOGW("未知命令: %s", cmd.c_str());
  }
}

bool Recorder::restart_budget_ok() {
  double now = mono_now();
  std::vector<double> keep;
  for (double t : restarts_) {
    if (now - t < cfg_.watchdog.restart_window_s) keep.push_back(t);
  }
  restarts_.swap(keep);
  return static_cast<int>(restarts_.size()) < cfg_.watchdog.max_restarts;
}

void Recorder::tick() {
  double now = mono_now();

  // 掉电恢复
  if (state_ == State::PowerFail) {
    bool power_ok = power_gpio_.fd() >= 0 ? !power_gpio_.active() : power_ok_since_ > 0;
    if (power_ok) {
      if (power_ok_since_ == 0) power_ok_since_ = now;
      if (now - power_ok_since_ >= cfg_.power_fail.recover_ms / 1000.0) {
        LOGI("电源已恢复，重新开始录像");
        state_ = State::Idle;
      }
    } else {
      power_ok_since_ = 0;
    }
  }

  // 管道意外退出
  int status = 0;
  if (pipe_.running() && pipe_.reap(&status)) {
    LOGE("采集管道退出（status=%d），%.0f 秒后重启", status, kRestartDelay);
    commit_open_segments();
    state_ = State::Idle;
    next_restart_at_ = now + kRestartDelay;
  }

  // 卡死检测：超过 3 个分段时长没有新分段
  const double stall = cfg_.storage.segment_seconds * 3.0 + 10;
  if (state_ == State::Recording) {
    double since = last_closed_at_ > 0 ? last_closed_at_ : started_at_;
    if (now - since > stall) {
      LOGE("%.0f 秒没有新分段，重启采集管道", now - since);
      stop_pipeline(cfg_.power_fail.finalize_timeout_ms);
      state_ = State::Idle;
      next_restart_at_ = now + kRestartDelay;
    }
  }

  // SD 卡插拔：目标目录变化时切换
  if (state_ == State::Recording) {
    const auto& s = cfg_.storage;
    std::string want = (!s.require_mount.empty() && !is_mounted(s.require_mount)) ? s.fallback_root : s.root;
    if (want != active_root_) {
      LOGI("存储位置变化 %s -> %s，切换录像目录", active_root_.c_str(), want.c_str());
      stop_pipeline(cfg_.power_fail.finalize_timeout_ms);
      state_ = State::Idle;
      next_restart_at_ = 0;
    }
  }

  // 启动 / 重启
  if (state_ == State::Idle && now >= next_restart_at_) {
    if (!restart_budget_ok()) {
      if (!wd_hold_) LOGE("短时间内重启次数过多，停止喂狗等待硬件复位");
      wd_hold_ = wd_.is_open();
      next_restart_at_ = now + 30;
    } else if (select_storage() && start_pipeline()) {
      restarts_.push_back(now);
    } else {
      next_restart_at_ = now + 5;
    }
  }

  if (!wd_hold_) wd_.kick();
}

void Recorder::write_status() {
  static const char* names[] = {"idle", "recording", "power_fail", "stopping"};
  nlohmann::json j;
  j["state"] = names[static_cast<int>(state_)];
  j["root"] = active_root_;
  j["segments_written"] = segments_written_;
  j["events_locked"] = events_locked_;
  j["pending_events"] = events_.pending();
  j["open_segments"] = open_.size();
  j["time"] = static_cast<long long>(time(nullptr));
  std::string path = cfg_.run_dir + "/status.json";
  std::string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "w");
  if (!f) return;
  std::string s = j.dump(2);
  fwrite(s.data(), 1, s.size(), f);
  fclose(f);
  rename(tmp.c_str(), path.c_str());
}

int Recorder::run() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGUSR1, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);

  if (cfg_.video.pipeline.find("mpegtsmux") != std::string::npos) ext_ = "ts";

  mkdirs(cfg_.run_dir);
  std::string ctl = cfg_.run_dir + "/ctl";
  unlink(ctl.c_str());
  if (mkfifo(ctl.c_str(), 0600) == 0) ctl_fd_ = open(ctl.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (ctl_fd_ < 0) LOGW("创建控制管道 %s 失败: %s", ctl.c_str(), strerror(errno));

  if (cfg_.power_fail.enabled && cfg_.power_fail.line >= 0) {
    if (power_gpio_.open(cfg_.power_fail.gpiochip, cfg_.power_fail.line, cfg_.power_fail.active_low))
      LOGI("掉电检测：%s 第 %d 脚", cfg_.power_fail.gpiochip.c_str(), cfg_.power_fail.line);
  }
  if (cfg_.watchdog.enabled) wd_.open(cfg_.watchdog.device, cfg_.watchdog.timeout_s);

  double last_status = 0;
  while (!g_quit) {
    struct pollfd fds[3];
    int n = 0;
    int pipe_idx = -1, ctl_idx = -1, gpio_idx = -1;
    if (pipe_.fd() >= 0) {
      pipe_idx = n;
      fds[n++] = {pipe_.fd(), POLLIN, 0};
    }
    if (ctl_fd_ >= 0) {
      ctl_idx = n;
      fds[n++] = {ctl_fd_, POLLIN, 0};
    }
    if (power_gpio_.fd() >= 0) {
      gpio_idx = n;
      fds[n++] = {power_gpio_.fd(), POLLIN, 0};
    }
    int r = poll(fds, n, 200);
    if (r > 0) {
      if (gpio_idx >= 0 && (fds[gpio_idx].revents & POLLIN)) {
        // 掉电信号优先处理
        if (power_gpio_.drain_events()) on_power_fail();
      }
      if (pipe_idx >= 0 && (fds[pipe_idx].revents & (POLLIN | POLLHUP))) pipe_.pump();
      if (ctl_idx >= 0 && (fds[ctl_idx].revents & POLLIN)) {
        char buf[256];
        ssize_t k = read(ctl_fd_, buf, sizeof(buf));
        if (k > 0) ctl_buf_.append(buf, static_cast<size_t>(k));
        size_t nl;
        while ((nl = ctl_buf_.find('\n')) != std::string::npos) {
          std::string cmd = ctl_buf_.substr(0, nl);
          ctl_buf_.erase(0, nl + 1);
          while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
          handle_command(cmd);
        }
      }
    }
    if (g_event) {
      g_event = 0;
      trigger_event();
    }
    tick();
    double now = mono_now();
    if (now - last_status >= 2.0) {
      write_status();
      last_status = now;
    }
  }

  LOGI("收到退出信号，停止录像");
  state_ = State::Stopping;
  stop_pipeline(3000);
  write_status();
  wd_.close();
  if (ctl_fd_ >= 0) close(ctl_fd_);
  unlink(ctl.c_str());
  return 0;
}

}  // namespace dvr
