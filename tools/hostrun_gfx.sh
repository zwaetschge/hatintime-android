#!/bin/sh
# usage: tools/hostrun_gfx.sh <name> <seconds> [-e VAR=value ...]
# Runs build/host/hathost with graphics: Xvfb + Mesa lavapipe (Vulkan on CPU) in hatintime-build.
# Screenshots every SHOT seconds (default 20) go to work/shots/<name>_NNN.png.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/tools/local.env" ] && . "$ROOT/tools/local.env"
name=$1; secs=$2; shift 2
docker rm -f hat-run-$name >/dev/null 2>&1
docker run --rm --name hat-run-$name -v "${HOST_ROOT:-$ROOT}":/w -w /w \
  -e W64_TIMEOUT=$secs -e W64_SCREEN=${W64_SCREEN:-1280x720} -e DISPLAY=:99 -e SHOT=${SHOT:-20} "$@" ${IMAGE:-hatintime-build} \
  sh -c 'Xvfb :99 -screen 0 ${W64_SCREEN}x24 -ac >/dev/null 2>&1 & sleep 1; mkdir -p work/user work/shots; rm -f work/shots/'$name'_*.png
         ( i=0; while sleep $SHOT; do i=$((i+1)); import -display :99 -window root work/shots/'$name'_$(printf %03d $i).png 2>/dev/null; done ) &
         VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
           W64_ARGS="${W64_ARGS:--NOSPLASH -WINDOWED -ResX=1280 -ResY=720}" \
           build/host/hathost /w/ref/game /w/work/user > work/'$name'.log 2>&1; echo EXIT $? >> work/'$name'.log'
tail -40 "$ROOT/work/$name.log"
