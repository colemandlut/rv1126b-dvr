#include "watchdog.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "log.h"

namespace dvr {

Watchdog::~Watchdog() { close(); }

bool Watchdog::open(const std::string& dev, int timeout_s) {
  fd_ = ::open(dev.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd_ < 0) {
    LOGE("打开看门狗 %s 失败: %s", dev.c_str(), strerror(errno));
    return false;
  }
  int t = timeout_s;
  if (ioctl(fd_, WDIOC_SETTIMEOUT, &t) < 0) LOGW("设置看门狗超时失败: %s", strerror(errno));
  LOGI("看门狗已启用，超时 %d 秒", t);
  return true;
}

void Watchdog::kick() {
  if (fd_ >= 0) ioctl(fd_, WDIOC_KEEPALIVE, 0);
}

void Watchdog::close() {
  if (fd_ < 0) return;
  ssize_t r = write(fd_, "V", 1);
  (void)r;
  ::close(fd_);
  fd_ = -1;
}

}  // namespace dvr
