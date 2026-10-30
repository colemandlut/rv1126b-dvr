#pragma once
#include <cstdint>
#include <string>

namespace dvr {

struct VideoConfig {
  std::string device = "/dev/video-camera0";
  int width = 1920;
  int height = 1080;
  int fps = 30;
  int bitrate_kbps = 4000;
  int gop = 30;
  int rotation = 0;  // 0/90/180/270，交给编码器或 RGA 处理
  // 完整的 gst-launch 管道模板，占位符见 pipeline.cpp 的 expand_template()
  std::string pipeline;
};

struct StorageConfig {
  std::string root = "/mnt/sdcard/DVR";       // 录像根目录（SD 卡）
  std::string require_mount = "/mnt/sdcard";  // 该挂载点未挂载时改用 fallback_root
  std::string fallback_root = "/userdata/dvr/rec";
  int segment_seconds = 60;
  uint64_t min_free_mb = 1024;  // 剩余空间低于此值时删除最旧的普通录像
  int event_max_percent = 20;   // 事件录像最多占用分区容量的百分比
  std::string channel = "ch1";
};

struct EventConfig {
  int pre_seconds = 10;
  int post_seconds = 20;
};

struct PowerFailConfig {
  bool enabled = false;
  std::string gpiochip = "/dev/gpiochip0";
  int line = -1;
  bool active_low = true;
  int finalize_timeout_ms = 1500;  // 掉电后等待封口的最长时间
  int recover_ms = 2000;           // 电源恢复多久后重新开始录像
};

struct WatchdogConfig {
  bool enabled = false;
  std::string device = "/dev/watchdog";
  int timeout_s = 15;
  int max_restarts = 5;          // 时间窗内管道重启超过此次数则停止喂狗，让硬件复位
  int restart_window_s = 600;
};

struct Config {
  VideoConfig video;
  StorageConfig storage;
  EventConfig event;
  PowerFailConfig power_fail;
  WatchdogConfig watchdog;
  std::string run_dir = "/run/dvr";  // 控制 FIFO 和状态文件
  std::string log_path = "/userdata/dvr/dvr.log";
  bool debug = false;
};

// 读取 JSON 配置；缺省字段用默认值。失败时返回 false 并在 err 中给出原因。
bool load_config(const std::string& path, Config& cfg, std::string& err);

}  // namespace dvr
