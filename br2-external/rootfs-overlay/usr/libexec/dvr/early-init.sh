#!/bin/sh
# 早期初始化：挂载虚拟文件系统和可写分区，准备配置。要尽快结束，录像在它之后才启动。

mount -a
mkdir -p /run/dvr /tmp/log

# 设备节点和热插拔
echo /sbin/mdev > /proc/sys/kernel/hotplug
mdev -s

# 按 GPT 分区名找块设备
part_dev() {
  for u in /sys/class/block/*/uevent; do
    if grep -q "^PARTNAME=$1\$" "$u"; then
      echo "/dev/$(basename "$(dirname "$u")")"
      return 0
    fi
  done
  return 1
}

# 挂载可写分区；只有在分区上完全没有文件系统时才格式化（第一次开机）
mount_rw() {
  name=$1 dir=$2 opts=$3
  dev=$(part_dev "$name") || { echo "early-init: 找不到分区 $name" > /dev/kmsg; return 1; }
  mkdir -p "$dir"
  if ! blkid "$dev" >/dev/null 2>&1; then
    echo "early-init: $name 没有文件系统，格式化为 ext4" > /dev/kmsg
    mkfs.ext4 -q -L "$name" "$dev"
  fi
  e2fsck -p "$dev" >/dev/null 2>&1
  mount -t ext4 -o "$opts" "$dev" "$dir"
}

mount_rw config /config noatime,data=journal
mount_rw data /data noatime
mkdir -p /data/log /data/rec /mnt/sdcard

# 第一次开机时放入默认配置
[ -f /config/dvr.json ] || { cp /usr/share/dvr/dvr.json /config/dvr.json.tmp && sync && mv /config/dvr.json.tmp /config/dvr.json; }

# 开机时 SD 卡已经插着的情况
for d in /dev/mmcblk1p1 /dev/mmcblk1; do
  [ -b "$d" ] && { MDEV=$(basename $d) ACTION=add /usr/libexec/dvr/sdcard.sh; break; }
done
exit 0
