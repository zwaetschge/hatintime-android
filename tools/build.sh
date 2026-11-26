#!/bin/sh
# One-shot build: your own A Hat in Time PC copy -> out/HatInTime-Android-<version>.apk
#   tools/build.sh /path/to/HatinTime          (game root containing Binaries/Win64/HatinTimeGame.exe)
#   tools/build.sh /path/to/HatinTimeGame.exe  (or the EXE directly)
#
# Run inside the build image (docker/Dockerfile), or on a Linux host with the same tools and
# ANDROID_SDK pointing at an SDK with NDK 27.3.13750724 and CMake 3.31.6.
# Optional signing env vars: HAT_KEYSTORE, HAT_KEYSTORE_PASSWORD, HAT_KEY_ALIAS, HAT_KEY_PASSWORD
# (otherwise a persistent debug keystore in work/release.keystore is generated and reused so later
# builds install cleanly over previous versions). JOBS sets parallel compiler jobs.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

IN=${1:?usage: tools/build.sh </path/to/HatinTime | /path/to/HatinTimeGame.exe>}
if [ -d "$IN" ]; then
  EXE="$IN/Binaries/Win64/HatinTimeGame.exe"
  [ -f "$EXE" ] || EXE="$IN/HatinTimeGame.exe"
else
  EXE="$IN"
fi
[ -f "$EXE" ] || { echo "HatinTimeGame.exe not found at: $EXE"; exit 1; }

WANT=0197f98641c2150c5e90e5e911f4464e38527c241e50817736ca7072c0b99892
HAVE=$(sha256sum "$EXE" | cut -d' ' -f1)
if [ "$HAVE" != "$WANT" ]; then
  echo "HatinTimeGame.exe SHA-256: $HAVE"
  echo "This port was built for:   $WANT"
  [ -n "$FORCE" ] || { echo "Other builds will most likely not work; set FORCE=1 to try anyway."; exit 1; }
fi

SDK=${ANDROID_SDK:-/sdk}
[ -d "$SDK/ndk/27.3.13750724" ] || { echo "ANDROID_SDK ($SDK) has no ndk/27.3.13750724"; exit 1; }

echo "== 1/6 third-party sources"
sh tools/fetch_deps.sh

echo "== 2/6 stage executable & layout"
mkdir -p ref/lift work/cat gen
cp "$EXE" ref/lift/HatinTimeGame.exe
printf '{\n  "HatinTimeGame.exe": "0x140000000"\n}\n' > ref/lift/layout.json

echo "== 3/6 function catalog & x86-64 -> C lift (a few minutes)"
python3 tools/pdata_catalog.py ref/lift/HatinTimeGame.exe work/cat/HatinTimeGame.txt
sh tools/lift_module.sh HatinTimeGame

echo "== 4/6 prepare DXVK Native D3D9 & compile built-in Vulkan shaders"
SRC=build/dxvk-src
if [ ! -f "$SRC/PREPARE_SHA256" ] || [ "$(cat "$SRC/PREPARE_SHA256")" != "$(sha256sum gfx/dxvk/prepare.py | cut -d' ' -f1)" ]; then
  git config --global --add safe.directory '*' 2>/dev/null || true
  python3 gfx/dxvk/prepare.py third_party/dxvk-native "$SRC"
  python3 gfx/dxvk/shaders.py "$SRC"
fi

echo "== 5/6 compile native Android libraries (arm64-v8a)"
sh tools/build_android_so.sh

echo "== 6/6 build & sign APK"
if [ -z "$HAT_KEYSTORE" ] && [ -z "$HP_KEYSTORE" ]; then
  KS="$ROOT/work/release.keystore"
  if [ ! -f "$KS" ]; then
    keytool -genkeypair -v -keystore "$KS" -storepass android -keypass android \
      -alias hatintime -keyalg RSA -keysize 2048 -validity 10000 \
      -dname "CN=HatInTimeAndroid, O=LocalBuild" >/dev/null 2>&1
  fi
  export HAT_KEYSTORE="$KS" HAT_KEYSTORE_PASSWORD=android HAT_KEY_ALIAS=hatintime HAT_KEY_PASSWORD=android
fi
echo "sdk.dir=$SDK" > android/local.properties
(cd android && sh ./gradlew -q --no-daemon assembleRelease)
VER=$(grep -o 'versionName = "[^"]*"' android/app/build.gradle.kts | cut -d'"' -f2)
mkdir -p out
cp android/app/build/outputs/apk/release/app-release.apk "out/HatInTime-Android-$VER.apk"
ls -lh "out/HatInTime-Android-$VER.apk"
sha256sum "out/HatInTime-Android-$VER.apk"
echo "done. Install with: adb install -r out/HatInTime-Android-$VER.apk"
