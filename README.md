# rv1126b-dvr

基于正点原子 RV1126B 开发板的车载 DVR 软件。功能规划见项目文档《RV1126B DVR 软件功能规划》。

## 当前进度：第一阶段（单路基础录像）

`dvr-record` 是录像进程，负责：

- 用 `gst-launch-1.0` 驱动采集和 MPP 硬件编码（第一阶段先用板上已有的 GStreamer，不依赖 SDK）
- 按固定时长分段写入 SD 卡，文件名带序号和开始时间：`<root>/normal/00000012_20261029_193000_ch1.ts`
- 剩余空间不足时删除最旧的普通录像（循环覆盖）
- 事件锁存：事件前后若干秒的分段移入 `<root>/event/`，不参与普通覆盖，最多占容量的 `event_max_percent`
- 掉电处理：掉电 GPIO 有效时让 GStreamer 发 EOS 封口，超时强杀，然后 `sync`；电源恢复后自动继续录
- 开机收回上次断电遗留在 `.tmp/` 的分段
- 采集管道异常退出或超过 3 个分段时长没有新分段时自动重启；短时间重启太多次则停止喂硬件看门狗
- SD 卡没插时临时录到 eMMC 的 `fallback_root`，插卡后自动切回

运行中可以往 `<run_dir>/ctl` 写命令：

```sh
echo event > /run/dvr/ctl      # 触发事件锁存（也可发 SIGUSR1）
echo powerfail > /run/dvr/ctl  # 模拟掉电
echo powerok > /run/dvr/ctl    # 模拟电源恢复
cat /run/dvr/status.json       # 当前状态
```

## 编译

设备端（x86_64 Linux 主机，Ubuntu 需 `g++-aarch64-linux-gnu nlohmann-json3-dev cmake`）：

```sh
cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake --build build-arm -j
aarch64-linux-gnu-strip build-arm/dvr-record
```

生成的是静态链接的 aarch64 可执行文件，直接拷到板子上即可运行，不依赖板上的 C++ 库版本。

宿主机测试：

```sh
cmake -B build-host -DDVR_TESTS=ON && cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
tests/run_sim.sh build-host/dvr-record   # 用假的 gst-launch 跑完整流程
```

## 安装到板子

```sh
cp dvr-record /usr/bin/
mkdir -p /etc/dvr && cp config/dvr.json /etc/dvr/
cp scripts/S99dvr /etc/init.d/ && /etc/init.d/S99dvr start
```

日志在 `/userdata/dvr/dvr.log`。

## 目录

| 路径 | 内容 |
| --- | --- |
| `src/recorder.*` | 主循环：分段、事件、掉电、重启、看门狗 |
| `src/storage.*` | 录像目录、命名、循环覆盖、事件配额 |
| `src/pipeline.*` | gst-launch 子进程与消息解析 |
| `src/gpio.*` `src/watchdog.*` | 掉电检测 GPIO、硬件看门狗 |
| `config/dvr.json` | 示例配置（支持 `//` 注释） |
| `scripts/S99dvr` | 开机自启脚本 |
