#!/bin/sh
# usage: tools/hostrun.sh <name> <seconds> [-e VAR=value ...]
# Runs build/host/hphost on ref/base (the extracted game) in the build image; log work/<name>.log,
# user dir work/user (C:\Users\Player\... maps below it). Unity's own log: W64_ARGS -logFile.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/tools/local.env" ] && . "$ROOT/tools/local.env"
name=$1; secs=$2; shift 2
docker rm -f hp-run-$name >/dev/null 2>&1
docker run --rm --name hp-run-$name -v "${HOST_ROOT:-$ROOT}":/w -w /w \
  -e W64_TIMEOUT=$secs "$@" ${IMAGE:-hp-dev:1} \
  sh -c "mkdir -p work/user; build/host/hphost /w/ref/base /w/work/user > work/$name.log 2>&1; echo EXIT \$? >> work/$name.log"
tail -40 "$ROOT/work/$name.log"
