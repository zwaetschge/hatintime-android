#!/bin/sh
# Build out/HatInTime-Android-<version>.apk in Docker from your own PC copy of A Hat in Time.
#   ./build.sh "/path/to/A Hat in Time"        (folder containing Binaries/Win64/HatinTimeGame.exe)
#   ./build.sh "/path/to/HatinTimeGame.exe"    (or the EXE directly)
# Optional env: JOBS=8, FORCE=1, IMAGE=hatintime-build,
#               HAT_KEYSTORE, HAT_KEYSTORE_PASSWORD, HAT_KEY_ALIAS, HAT_KEY_PASSWORD
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)
IN=${1:?usage: ./build.sh </path/to/A Hat in Time | /path/to/HatinTimeGame.exe>}
IMAGE=${IMAGE:-hatintime-build}

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "== building Docker image $IMAGE (first run only)"
  docker build -t "$IMAGE" "$ROOT/docker"
fi

if [ -d "$IN" ]; then
  MOUNT=$(cd "$IN" && pwd)
  ARG=/game
else
  MOUNT=$(cd "$(dirname "$IN")" && pwd)
  ARG="/game/$(basename "$IN")"
fi

exec docker run --rm \
  -v "$ROOT":/w \
  -v "$MOUNT":/game:ro \
  -w /w \
  -e JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}" \
  -e FORCE="${FORCE:-}" \
  -e HAT_KEYSTORE="${HAT_KEYSTORE:-}" \
  -e HAT_KEYSTORE_PASSWORD="${HAT_KEYSTORE_PASSWORD:-}" \
  -e HAT_KEY_ALIAS="${HAT_KEY_ALIAS:-}" \
  -e HAT_KEY_PASSWORD="${HAT_KEY_PASSWORD:-}" \
  "$IMAGE" sh tools/build.sh "$ARG"
