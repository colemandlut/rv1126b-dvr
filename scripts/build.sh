#!/bin/bash
# 用 SDK 的 Buildroot 编出 DVR 专用系统的根文件系统
#
# 用法: scripts/build.sh <SDK 目录> <debug|release> [SDK 的基础 defconfig]
#   例: scripts/build.sh ~/rv1126b-sdk debug
#
# 输出: <SDK>/buildroot/output/dvr-<版本>/images/rootfs.squashfs
# 内核、U-Boot 和打包成 update.img 的步骤等 SDK 到手后，对照它自带的 build.sh 接上（TODO）。
set -euo pipefail

SDK=$(cd "${1:?缺少 SDK 目录}" && pwd)
VARIANT=${2:?缺少版本：debug 或 release}
BASE=${3:-}
HERE=$(cd "$(dirname "$0")/.." && pwd)
EXT=$HERE/br2-external
OUT=$SDK/buildroot/output/dvr-$VARIANT

case $VARIANT in debug|release) ;; *) echo "版本只能是 debug 或 release" >&2; exit 2 ;; esac

if [ -z "$BASE" ]; then
  # SDK 里 RV1126B 的默认 defconfig，名字以实际 SDK 为准
  BASE=$(cd "$SDK/buildroot/configs" && ls rockchip_rv1126b*_defconfig 2>/dev/null | head -1 || true)
  [ -n "$BASE" ] || { echo "找不到 RV1126B 的基础 defconfig，请作为第 3 个参数指定" >&2; exit 1; }
fi
echo "基础配置: $BASE，版本: $VARIANT，输出: $OUT"

cd "$SDK/buildroot"
make O="$OUT" BR2_EXTERNAL="$EXT" "$BASE"
support/kconfig/merge_config.sh -m -O "$OUT" "$OUT/.config" \
  "$EXT/configs/dvr_common.fragment" "$EXT/configs/dvr_$VARIANT.fragment"
make O="$OUT" BR2_EXTERNAL="$EXT" olddefconfig

# 合并后检查片段里的选项都生效了，没生效的多半是包名不对（[待核对]）
missing=0
for f in "$EXT/configs/dvr_common.fragment" "$EXT/configs/dvr_$VARIANT.fragment"; do
  while IFS= read -r line; do
    case $line in BR2_*=*) grep -qxF "$line" "$OUT/.config" || { echo "未生效: $line"; missing=1; } ;; esac
  done < "$f"
done
[ $missing -eq 0 ] || echo "警告：上面这些选项没有生效，请核对包名或依赖"

make O="$OUT" BR2_EXTERNAL="$EXT" -j"$(nproc)"
ls -l "$OUT/images/"
