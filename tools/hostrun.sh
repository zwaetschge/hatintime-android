#!/bin/sh
# usage: tools/hostrun.sh <name> <seconds> [-e VAR=value ...]
# Runs build/host/hathost on ref/game in the build image; log work/<name>.log,
# user dir work/user (C:\Users\Player\... maps below it).
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/tools/local.env" ] && . "$ROOT/tools/local.env"
name=$1; secs=$2; shift 2
docker rm -f hat-run-$name >/dev/null 2>&1
docker run --rm --name hat-run-$name -v "${HOST_ROOT:-$ROOT}":/w -w /w \
  -e W64_TIMEOUT=$secs "$@" ${IMAGE:-hatintime-build} \
  sh -c "mkdir -p work/user; build/host/hathost /w/ref/game /w/work/user > work/$name.log 2>&1; echo EXIT \$? >> work/$name.log"
tail -40 "$ROOT/work/$name.log"
