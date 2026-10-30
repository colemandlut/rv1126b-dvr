#!/bin/sh
# mdev 调用：SD 卡插入时以 exFAT 挂载到 /mnt/sdcard，拔出时卸载
# dvr-record 会自己发现挂载点变化并切换录像目录

MNT=/mnt/sdcard
DEV=/dev/$MDEV

case "$ACTION" in
  add|"")
    grep -q " $MNT " /proc/mounts && exit 0
    # 整卡没有分区表时直接用 mmcblk1，有分区时用 mmcblk1p1
    [ "$MDEV" = mmcblk1 ] && [ -b /dev/mmcblk1p1 ] && exit 0
    mkdir -p $MNT
    if mount -t exfat -o noatime,iocharset=utf8,errors=remount-ro "$DEV" $MNT 2>/dev/null; then
      echo "sdcard: $DEV 已挂载" > /dev/kmsg
    else
      echo "sdcard: $DEV 不是 exFAT，未挂载" > /dev/kmsg
    fi
    ;;
  remove)
    grep -q "^$DEV $MNT " /proc/mounts && umount -l $MNT
    ;;
esac
exit 0
