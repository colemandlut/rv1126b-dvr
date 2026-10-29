#include "config.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace dvr {

using nlohmann::json;

namespace {

template <typename T>
void get(const json& j, const char* key, T& out) {
  auto it = j.find(key);
  if (it != j.end() && !it->is_null()) out = it->get<T>();
}

}  // namespace

bool load_config(const std::string& path, Config& cfg, std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "无法打开配置文件 " + path;
    return false;
  }
  json j;
  try {
    j = json::parse(in, nullptr, true, /*ignore_comments=*/true);
  } catch (const std::exception& e) {
    err = std::string("配置文件格式错误: ") + e.what();
    return false;
  }

  try {
    if (j.contains("video")) {
      const auto& v = j["video"];
      get(v, "device", cfg.video.device);
      get(v, "width", cfg.video.width);
      get(v, "height", cfg.video.height);
      get(v, "fps", cfg.video.fps);
      get(v, "bitrate_kbps", cfg.video.bitrate_kbps);
      get(v, "gop", cfg.video.gop);
      get(v, "rotation", cfg.video.rotation);
      get(v, "pipeline", cfg.video.pipeline);
    }
    if (j.contains("storage")) {
      const auto& s = j["storage"];
      get(s, "root", cfg.storage.root);
      get(s, "require_mount", cfg.storage.require_mount);
      get(s, "fallback_root", cfg.storage.fallback_root);
      get(s, "segment_seconds", cfg.storage.segment_seconds);
      get(s, "min_free_mb", cfg.storage.min_free_mb);
      get(s, "event_max_percent", cfg.storage.event_max_percent);
      get(s, "channel", cfg.storage.channel);
    }
    if (j.contains("event")) {
      get(j["event"], "pre_seconds", cfg.event.pre_seconds);
      get(j["event"], "post_seconds", cfg.event.post_seconds);
    }
    if (j.contains("power_fail")) {
      const auto& p = j["power_fail"];
      get(p, "enabled", cfg.power_fail.enabled);
      get(p, "gpiochip", cfg.power_fail.gpiochip);
      get(p, "line", cfg.power_fail.line);
      get(p, "active_low", cfg.power_fail.active_low);
      get(p, "finalize_timeout_ms", cfg.power_fail.finalize_timeout_ms);
      get(p, "recover_ms", cfg.power_fail.recover_ms);
    }
    if (j.contains("watchdog")) {
      const auto& w = j["watchdog"];
      get(w, "enabled", cfg.watchdog.enabled);
      get(w, "device", cfg.watchdog.device);
      get(w, "timeout_s", cfg.watchdog.timeout_s);
      get(w, "max_restarts", cfg.watchdog.max_restarts);
      get(w, "restart_window_s", cfg.watchdog.restart_window_s);
    }
    get(j, "run_dir", cfg.run_dir);
    get(j, "log_path", cfg.log_path);
    get(j, "debug", cfg.debug);
  } catch (const std::exception& e) {
    err = std::string("配置字段类型错误: ") + e.what();
    return false;
  }

  if (cfg.storage.segment_seconds < 5 || cfg.storage.segment_seconds > 600) {
    err = "storage.segment_seconds 必须在 5～600 之间";
    return false;
  }
  if (cfg.storage.event_max_percent < 0 || cfg.storage.event_max_percent > 90) {
    err = "storage.event_max_percent 必须在 0～90 之间";
    return false;
  }
  if (cfg.video.pipeline.empty()) {
    err = "video.pipeline 不能为空";
    return false;
  }
  return true;
}

}  // namespace dvr
