#pragma once
#include <functional>
#include <map>
#include <string>
#include <sys/types.h>

namespace dvr {

// 把 {key} 占位符替换成对应值，未知占位符保持原样
std::string expand_template(const std::string& tpl, const std::map<std::string, std::string>& vars);

// 解析 gst-launch -m 打印的 splitmuxsink 消息：
//   Got message #N from element "splitmuxsink0" (element): splitmuxsink-fragment-closed, location=(string)/x/seg00001.mp4, running-time=(guint64)...;
// kind 返回 "opened" 或 "closed"
bool parse_split_message(const std::string& line, std::string& kind, std::string& location);

// 以子进程方式运行 gst-launch-1.0，读取其输出
class Pipeline {
 public:
  using LineHandler = std::function<void(const std::string&)>;

  ~Pipeline();
  bool start(const std::string& launch_args, LineHandler on_line);
  // 读出当前可读的输出（非阻塞），逐行回调
  void pump();
  int fd() const { return fd_; }
  bool running() const { return pid_ > 0; }
  // 子进程已退出时回收并返回 true
  bool reap(int* status = nullptr);
  // 发 SIGINT 让 gst-launch -e 发送 EOS 并封口文件，超时后 SIGKILL
  void stop(int timeout_ms);

 private:
  pid_t pid_ = -1;
  int fd_ = -1;
  std::string buf_;
  LineHandler on_line_;
};

}  // namespace dvr
