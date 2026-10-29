#include "gpio.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "log.h"

namespace dvr {

GpioInput::~GpioInput() {
  if (fd_ >= 0) close(fd_);
}

bool GpioInput::open(const std::string& chip, int line, bool active_low) {
  int cfd = ::open(chip.c_str(), O_RDONLY | O_CLOEXEC);
  if (cfd < 0) {
    LOGE("打开 %s 失败: %s", chip.c_str(), strerror(errno));
    return false;
  }
  struct gpio_v2_line_request req;
  memset(&req, 0, sizeof(req));
  req.offsets[0] = static_cast<__u32>(line);
  req.num_lines = 1;
  strncpy(req.consumer, "dvr-powerfail", sizeof(req.consumer) - 1);
  req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
  if (active_low) req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
  int r = ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req);
  close(cfd);
  if (r < 0) {
    LOGE("申请 GPIO %s:%d 失败: %s", chip.c_str(), line, strerror(errno));
    return false;
  }
  fd_ = req.fd;
  return true;
}

bool GpioInput::active() const {
  struct gpio_v2_line_values v;
  memset(&v, 0, sizeof(v));
  v.mask = 1;
  if (ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return false;
  return (v.bits & 1) != 0;
}

bool GpioInput::drain_events() {
  struct gpio_v2_line_event ev[16];
  while (read(fd_, ev, sizeof(ev)) > 0) {
  }
  return active();
}

}  // namespace dvr
