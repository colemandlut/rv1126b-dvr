#pragma once
#include <string>

namespace dvr {

// 硬件看门狗：定期 kick()；程序正常退出时 close() 写 'V' 关闭看门狗
class Watchdog {
 public:
  ~Watchdog();
  bool open(const std::string& dev, int timeout_s);
  void kick();
  void close();
  bool is_open() const { return fd_ >= 0; }

 private:
  int fd_ = -1;
};

}  // namespace dvr
