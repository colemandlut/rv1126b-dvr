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
cmake -S app -B build-arm -DCMAKE_TOOLCHAIN_FILE=app/cmake/aarch64-linux-gnu.cmake
cmake --build build-arm -j
aarch64-linux-gnu-strip build-arm/dvr-record
```

生成的是静态链接的 aarch64 可执行文件，直接拷到板子上即可运行，不依赖板上的 C++ 库版本。

宿主机测试：

```sh
cmake -S app -B build-host -DDVR_TESTS=ON && cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
app/tests/run_sim.sh build-host/dvr-record   # 用假的 gst-launch 跑完整流程
```

## 专用系统（进行中）

不在出厂系统上开发，改为用 SDK 的 Buildroot 编一套 DVR 专用精简系统：squashfs 只读根、A/B 双系统、录像优先启动。方案见《RV1126B DVR 专用系统架构方案》。

```sh
scripts/build.sh <SDK 目录> debug      # 调试版：串口 shell、SSH、调试工具
scripts/build.sh <SDK 目录> release    # 发布版：关闭串口登录和 SSH
```

| 路径 | 内容 |
| --- | --- |
| `app/` | 应用程序源码、测试、示例配置 |
| `br2-external/` | Buildroot 外部目录：配置片段、dvr-app 包、根文件系统覆盖层、post-build |
| `br2-external/rootfs-overlay/etc/inittab` | 启动顺序：早期挂载 → 3A → dvr-record → 4G → 其他 |
| `board/kernel/dvr.config` | 内核裁剪片段 |
| `board/parameter.txt` | 分区表草案（A/B、config、data） |
| `scripts/build.sh` | 一条命令编根文件系统（内核和打包待 SDK 到手后接上） |

## 在出厂系统上测试（开发初期用）

```sh
cp dvr-record /usr/bin/
mkdir -p /etc/dvr && cp app/config/dvr.json /etc/dvr/
/usr/bin/dvr-record -c /etc/dvr/dvr.json &
```

日志在 `/userdata/dvr/dvr.log`。

## 应用源码

| 路径 | 内容 |
| --- | --- |
| `app/src/recorder.*` | 主循环：分段、事件、掉电、重启、看门狗 |
| `app/src/storage.*` | 录像目录、命名、循环覆盖、事件配额 |
| `app/src/pipeline.*` | gst-launch 子进程与消息解析 |
| `app/src/gpio.*` `app/src/watchdog.*` | 掉电检测 GPIO、硬件看门狗 |
| `app/config/dvr.json` | 示例配置（支持 `//` 注释） |
