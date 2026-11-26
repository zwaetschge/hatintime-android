#!/bin/sh
# Fetch the pinned third-party sources used by the build into third_party/ (never committed):
#   - pcrecomp (MIT, x86-64 -> C lifter) at commit 35548b8 + tools/patches/pcrecomp-hatintime.patch
#   - dxvk-native (zlib, Direct3D 9 -> Vulkan) at tag native-1.9.2b
#   - SDL 3.2.12 (zlib, Android window/input/gamepad/audio)
#   - stb_vorbis.c v1.22 (MIT / public domain, Ogg Vorbis audio decoder)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TP=$ROOT/third_party
PCRECOMP_REPO=https://github.com/sp00nznet/pcrecomp
PCRECOMP_COMMIT=35548b8d0fd8aa853da67edcf7cd6134f6b9cdc7
DXVK_REPO=https://github.com/Joshua-Ashton/dxvk-native.git
DXVK_TAG=native-1.9.2b
STB_COMMIT=6b5799bae8a1fe3792691ef4b72ae26434c3e799

mkdir -p "$TP/dl" "$TP/stb"
cd "$TP"
get() { [ -s "$2" ] || curl -fsSL -o "$2" "$1"; }

if [ ! -d pcrecomp/.git ]; then
  git clone -q "$PCRECOMP_REPO" pcrecomp
  (cd pcrecomp && git checkout -q "$PCRECOMP_COMMIT" && git apply "$ROOT/tools/patches/pcrecomp-hatintime.patch")
  echo "pcrecomp $PCRECOMP_COMMIT + hatintime patch"
fi
if [ ! -d dxvk-native/.git ]; then
  git clone -q --depth 1 --branch "$DXVK_TAG" "$DXVK_REPO" dxvk-native
  echo "dxvk-native $DXVK_TAG"
fi
[ -d SDL3-3.2.12 ] || {
  get https://github.com/libsdl-org/SDL/releases/download/release-3.2.12/SDL3-3.2.12.tar.gz dl/SDL3-3.2.12.tar.gz
  tar xzf dl/SDL3-3.2.12.tar.gz
}
get "https://raw.githubusercontent.com/nothings/stb/$STB_COMMIT/stb_vorbis.c" stb/stb_vorbis.c
echo "third_party ready"
