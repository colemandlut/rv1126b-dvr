#!/bin/bash
# 在 PC 上用假的 gst-launch 跑一遍完整流程：分段、事件锁存、模拟掉电与恢复
# 用法: tests/run_sim.sh <宿主机编译出的 dvr-record>
set -e
BIN=$(readlink -f "$1")
SRC=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
mkdir -p "$W/bin"
cp "$SRC/fake-gst-launch-1.0" "$W/bin/gst-launch-1.0"
cat > "$W/dvr.json" <<JSON
{ "video": { "pipeline": "fake ! splitmuxsink location={location} max-size-time={segment_ns}" },
  "storage": { "root": "$W/rec", "require_mount": "", "segment_seconds": 5, "min_free_mb": 0 },
  "event": { "pre_seconds": 3, "post_seconds": 3 },
  "run_dir": "$W/run", "log_path": "$W/dvr.log" }
JSON
PATH="$W/bin:$PATH" "$BIN" -c "$W/dvr.json" 2>/dev/null &
PID=$!
sleep 7; echo event > "$W/run/ctl"
sleep 5; echo powerfail > "$W/run/ctl"
sleep 2; grep -q power_fail "$W/run/status.json"
echo powerok > "$W/run/ctl"
sleep 5; kill -TERM $PID; wait $PID

n_event=$(ls "$W/rec/event" | wc -l)
n_normal=$(ls "$W/rec/normal" | wc -l)
n_tmp=$(ls "$W/rec/.tmp" | wc -l)
echo "普通 $n_normal 段，事件 $n_event 段，遗留 $n_tmp 个"
grep -q "电源已恢复" "$W/dvr.log"
[ "$n_event" -ge 2 ] && [ "$n_normal" -ge 2 ] && [ "$n_tmp" -eq 0 ]
rm -rf "$W"
echo "模拟测试通过"
