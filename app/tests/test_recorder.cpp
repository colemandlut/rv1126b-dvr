#include "check.h"
#include "config.h"
#include "pipeline.h"
#include "recorder.h"

using namespace dvr;

int main() {
  // gst-launch -m 消息解析
  std::string kind, loc;
  CHECK(parse_split_message(
      "Got message #127 from element \"splitmuxsink0\" (element): splitmuxsink-fragment-closed, "
      "location=(string)/mnt/sdcard/DVR/.tmp/seg00003.mp4, running-time=(guint64)30000000000;",
      kind, loc));
  CHECK(kind == "closed");
  CHECK(loc == "/mnt/sdcard/DVR/.tmp/seg00003.mp4");
  CHECK(parse_split_message(
      "splitmuxsink-fragment-opened, location=(string)\"/a b/seg00001.ts\", running-time=(guint64)0;", kind, loc));
  CHECK(kind == "opened");
  CHECK(loc == "/a b/seg00001.ts");
  // 板上实测的原文（带 sink 字段）
  CHECK(parse_split_message(
      "Got message #140 from element \"splitmuxsink0\" (element): splitmuxsink-fragment-closed, "
      "location=(string)/userdata/dvrtest/seg000.mp4, running-time=(guint64)9116602723, "
      "sink=(GstElement)\"\\(GstFileSink\\)\\ sink\";",
      kind, loc));
  CHECK(kind == "closed");
  CHECK(loc == "/userdata/dvrtest/seg000.mp4");
  CHECK(!parse_split_message("Setting pipeline to PLAYING ...", kind, loc));

  // 模板替换
  CHECK(expand_template("v4l2src device={device} ! x bps={bitrate} {unknown}",
                        {{"device", "/dev/video0"}, {"bitrate", "4000000"}}) ==
        "v4l2src device=/dev/video0 ! x bps=4000000 {unknown}");

  // 事件窗口：t=1000，前 10 后 20 -> [990, 1020]
  EventLocker ev;
  ev.add(1000, 10, 20);
  CHECK(ev.overlaps(940, 1000));   // 事件发生时正在写的段
  CHECK(ev.overlaps(930, 991));    // 预录窗口覆盖上一段的最后 1 秒
  CHECK(!ev.overlaps(930, 990));   // 只在边界相接不算重叠
  CHECK(!ev.overlaps(900, 989));   // 更早的段不锁
  CHECK(ev.overlaps(1000, 1060));  // 后续 20 秒落在下一段
  CHECK(!ev.overlaps(1020, 1080));
  ev.prune(1019);
  CHECK(ev.pending() == 1);
  ev.prune(1021);
  CHECK(ev.pending() == 0);
  // 仓库里的示例配置能正常解析
  Config cfg;
  std::string err;
  CHECK(load_config(DVR_SAMPLE_CONFIG, cfg, err));
  CHECK(cfg.storage.segment_seconds == 60);
  CHECK(cfg.video.pipeline.find("splitmuxsink") != std::string::npos);
  TEST_MAIN_END();
}
