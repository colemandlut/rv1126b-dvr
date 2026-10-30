#include <cstdio>
#include <cstring>
#include <string>

#include "config.h"
#include "log.h"
#include "recorder.h"
#include "storage.h"

static void usage() {
  fprintf(stderr,
          "用法: dvr-record [-c 配置文件]\n"
          "  默认配置文件 /etc/dvr/dvr.json\n"
          "运行中可向 <run_dir>/ctl 写命令: event | powerfail | powerok | restart\n");
}

int main(int argc, char** argv) {
  std::string cfg_path = "/etc/dvr/dvr.json";
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-c") && i + 1 < argc) {
      cfg_path = argv[++i];
    } else {
      usage();
      return 2;
    }
  }

  dvr::Config cfg;
  std::string err;
  if (!dvr::load_config(cfg_path, cfg, err)) {
    fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  auto slash = cfg.log_path.rfind('/');
  if (slash != std::string::npos) dvr::mkdirs(cfg.log_path.substr(0, slash));
  dvr::log_init(cfg.log_path, 1 << 20, 3, cfg.debug ? dvr::LogLevel::Debug : dvr::LogLevel::Info);
  LOGI("dvr-record 启动，配置 %s", cfg_path.c_str());

  dvr::Recorder rec(cfg);
  return rec.run();
}
