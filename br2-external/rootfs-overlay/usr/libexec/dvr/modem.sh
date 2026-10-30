#!/bin/sh
# 4G 拨号：等 EC25 的 QMI 设备出现后用 quectel-CM 拨号；退出后由 init 重新拉起
# APN 写在 /config/modem.conf，例如 APN=povo.jp

APN=povo.jp
[ -f /config/modem.conf ] && . /config/modem.conf

i=0
while [ ! -e /dev/cdc-wdm0 ]; do
  i=$((i + 1))
  [ $i -gt 60 ] && exec sleep 60  # 没插模块：一分钟后由 init 再试
  sleep 1
done
exec quectel-CM -s "$APN"
