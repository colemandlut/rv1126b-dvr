#!/bin/sh
# 正常关机：先停录像让当前分段封口，再同步卸载
killall -TERM dvr-record 2>/dev/null
for i in 1 2 3 4 5 6 7 8 9 10; do pidof dvr-record >/dev/null || break; sleep 0.3; done
sync
umount -l /mnt/sdcard 2>/dev/null
umount /data /config 2>/dev/null
exit 0
