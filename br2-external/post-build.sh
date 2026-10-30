#!/bin/sh
# Buildroot post-build：$1 = target 目录。只读根文件系统的最后整理。
set -e
TARGET=$1

# 我们的 inittab 不跑 /etc/init.d，Rockchip 各包装进来的启动脚本一律去掉
rm -f "$TARGET"/etc/init.d/S[0-9]*

# 只读根上需要写的位置指向可写分区或内存
ln -sfn /config/dropbear "$TARGET/etc/dropbear"
ln -sfn ../tmp/resolv.conf "$TARGET/etc/resolv.conf"

# 调试版才在串口上开放登录 shell
if grep -q '^BR2_PACKAGE_DVR_APP_DEBUG_SHELL=y' "$BR2_CONFIG"; then
  grep -q ttyFIQ0 "$TARGET/etc/inittab" ||
    echo 'ttyFIQ0::respawn:/sbin/getty -L ttyFIQ0 1500000 vt100 -n -l /bin/sh' >> "$TARGET/etc/inittab"
else
  sed -i '/ttyFIQ0/d' "$TARGET/etc/inittab"
  # 发布版锁定 root 密码
  sed -i 's#^root:[^:]*:#root:*:#' "$TARGET/etc/shadow"
fi
