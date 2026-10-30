#!/bin/sh
# 录像起来以后再做的事：WiFi、校时、调试版的 SSH

if [ -f /config/wpa_supplicant.conf ]; then
  wpa_supplicant -B -i wlan0 -c /config/wpa_supplicant.conf && udhcpc -b -i wlan0 -q >/dev/null 2>&1 &
fi
[ -x /usr/sbin/chronyd ] && chronyd -f /etc/chrony.conf
[ -x /usr/sbin/dropbear ] && mkdir -p /config/dropbear && dropbear -R
exit 0
