#pragma once
#include <string>

namespace dvr {

// 用 Linux GPIO 字符设备（v2 接口）监听一根输入线的电平变化，用于掉电检测
class GpioInput {
 public:
  ~GpioInput();
  bool open(const std::string& chip, int line, bool active_low);
  int fd() const { return fd_; }
  // 读取当前是否处于“有效”状态（active_low 已考虑在内）
  bool active() const;
  // 读出排队的边沿事件，返回事件后线路是否有效
  bool drain_events();

 private:
  int fd_ = -1;
};

}  // namespace dvr
