#include "pipeline.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

namespace dvr {

std::string expand_template(const std::string& tpl, const std::map<std::string, std::string>& vars) {
  std::string out;
  out.reserve(tpl.size());
  for (size_t i = 0; i < tpl.size();) {
    if (tpl[i] == '{') {
      size_t end = tpl.find('}', i);
      if (end != std::string::npos) {
        auto it = vars.find(tpl.substr(i + 1, end - i - 1));
        if (it != vars.end()) {
          out += it->second;
          i = end + 1;
          continue;
        }
      }
    }
    out += tpl[i++];
  }
  return out;
}

bool parse_split_message(const std::string& line, std::string& kind, std::string& location) {
  static const char kPrefix[] = "splitmuxsink-fragment-";
  size_t p = line.find(kPrefix);
  if (p == std::string::npos) return false;
  p += sizeof(kPrefix) - 1;
  if (line.compare(p, 6, "opened") == 0) {
    kind = "opened";
  } else if (line.compare(p, 6, "closed") == 0) {
    kind = "closed";
  } else {
    return false;
  }
  size_t l = line.find("location=", p);
  if (l == std::string::npos) return false;
  l += 9;
  if (line.compare(l, 8, "(string)") == 0) l += 8;
  bool quoted = l < line.size() && line[l] == '"';
  if (quoted) ++l;
  size_t e = quoted ? line.find('"', l) : line.find_first_of(",;", l);
  location = line.substr(l, e == std::string::npos ? std::string::npos : e - l);
  return !location.empty();
}

Pipeline::~Pipeline() { stop(500); }

bool Pipeline::start(const std::string& launch_args, LineHandler on_line) {
  int fds[2];
  if (pipe(fds) != 0) return false;
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return false;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    close(fds[1]);
    setpgid(0, 0);
    std::string cmd = "exec gst-launch-1.0 -e -m " + launch_args;
    execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  pid_ = pid;
  fd_ = fds[0];
  buf_.clear();
  on_line_ = std::move(on_line);
  LOGI("启动采集管道 pid=%d", pid);
  LOGD("gst-launch-1.0 -e -m %s", launch_args.c_str());
  return true;
}

void Pipeline::pump() {
  if (fd_ < 0) return;
  char tmp[4096];
  for (;;) {
    ssize_t n = read(fd_, tmp, sizeof(tmp));
    if (n > 0) {
      buf_.append(tmp, static_cast<size_t>(n));
      size_t nl;
      while ((nl = buf_.find('\n')) != std::string::npos) {
        std::string line = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        if (on_line_) on_line_(line);
      }
      continue;
    }
    if (n == 0) {  // 子进程关闭了输出
      close(fd_);
      fd_ = -1;
    }
    break;
  }
}

bool Pipeline::reap(int* status) {
  if (pid_ <= 0) return false;
  int st = 0;
  pid_t r = waitpid(pid_, &st, WNOHANG);
  if (r != pid_) return false;
  pump();
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  pid_ = -1;
  if (status) *status = st;
  return true;
}

void Pipeline::stop(int timeout_ms) {
  if (pid_ <= 0) return;
  kill(pid_, SIGINT);
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (;;) {
    if (fd_ >= 0) {
      struct pollfd p {fd_, POLLIN, 0};
      poll(&p, 1, 20);
      pump();
    } else {
      usleep(20000);
    }
    if (reap()) return;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    long ms = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
    if (ms > timeout_ms) break;
  }
  LOGW("采集管道未在 %d ms 内退出，强制结束", timeout_ms);
  kill(-pid_, SIGKILL);
  kill(pid_, SIGKILL);
  int st;
  waitpid(pid_, &st, 0);
  pump();
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  pid_ = -1;
}

}  // namespace dvr
