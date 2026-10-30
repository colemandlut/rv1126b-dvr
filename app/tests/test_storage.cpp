#include <fstream>
#include <string>
#include <unistd.h>

#include "check.h"
#include "log.h"
#include "storage.h"

using namespace dvr;

// 用文件总大小模拟一张容量很小的卡
class FakeStorage : public Storage {
 public:
  uint64_t capacity = 0;
  uint64_t used() const {
    uint64_t u = 0;
    for (bool ev : {false, true})
      for (const auto& s : list(ev)) u += s.size;
    return u;
  }

 protected:
  uint64_t free_bytes() const override { return capacity - used(); }
  uint64_t total_bytes() const override { return capacity; }
};

static void write_file(const std::string& path, size_t bytes) {
  std::ofstream f(path, std::ios::binary);
  std::string data(bytes, 'x');
  f << data;
}

int main() {
  log_init("", 0, 1, LogLevel::Error);

  // 文件名往返
  time_t t = 1798000000;
  std::string name = Storage::make_name(42, t, "ch1", "mp4");
  uint64_t seq;
  time_t back;
  CHECK(Storage::parse_name(name, seq, back));
  CHECK(seq == 42);
  CHECK(back == t);
  CHECK(!Storage::parse_name("readme.txt", seq, back));

  char tmpl[] = "/tmp/dvrtestXXXXXX";
  std::string root = mkdtemp(tmpl);

  FakeStorage st;
  st.capacity = 10000;
  CHECK(st.init(root, "ch1", "mp4"));
  CHECK(st.next_seq() == 1);

  // 提交 8 个 1000 字节分段
  std::vector<Segment> segs;
  for (int i = 0; i < 8; ++i) {
    std::string tmp = st.tmp_dir() + "/seg" + std::to_string(i) + ".mp4";
    write_file(tmp, 1000);
    Segment s;
    CHECK(st.commit(tmp, t + i * 60, 60, s));
    segs.push_back(s);
  }
  CHECK(st.list(false).size() == 8);
  CHECK(access((st.tmp_dir() + "/seg0.mp4").c_str(), F_OK) != 0);

  // 锁存第 2 段
  CHECK(st.lock(segs[1]));
  CHECK(st.list(true).size() == 1);
  CHECK(st.list(false).size() == 7);

  // 要求剩余 4000：已用 8000，需删 2 个最旧普通段（序号 1、3），事件段保留
  int del = st.enforce_space(4000, 50);
  CHECK(del == 2);
  auto normal = st.list(false);
  CHECK(normal.size() == 5);
  CHECK(normal.front().seq == 4);
  CHECK(st.list(true).size() == 1);

  // 事件配额 10% = 1000 字节：再锁 1 段后超额，应删最旧的事件段
  CHECK(st.lock(normal[0]));
  st.enforce_space(0, 10);
  auto events = st.list(true);
  CHECK(events.size() == 1);
  CHECK(events.front().seq == 4);

  // 重新 init 后序号继续递增
  FakeStorage st2;
  st2.capacity = 10000;
  CHECK(st2.init(root, "ch1", "mp4"));
  CHECK(st2.next_seq() == 9);

  // 收回 .tmp 遗留分段，空文件直接删掉
  write_file(st2.tmp_dir() + "/seg00099.mp4", 500);
  write_file(st2.tmp_dir() + "/seg00100.mp4", 0);
  st2.mark_open(st2.tmp_dir() + "/seg00099.mp4", t + 3600);
  CHECK(st2.recover_tmp(60) == 1);
  CHECK(access((st2.tmp_dir() + "/seg00100.mp4").c_str(), F_OK) != 0);
  CHECK(access((st2.tmp_dir() + "/seg00099.mp4.start").c_str(), F_OK) != 0);
  CHECK(st2.list(false).back().seq == 9);
  CHECK(st2.list(false).back().start == t + 3600);  // 用记录的开始时间命名

  std::string cmd = "rm -rf " + root;
  CHECK(system(cmd.c_str()) == 0);
  TEST_MAIN_END();
}
