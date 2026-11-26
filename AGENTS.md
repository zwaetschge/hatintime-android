# Project: hatintime-android

Static recompilation of **A Hat in Time** (PC Win64, Unreal Engine 3, Direct3D 9 SM3) for Android arm64.
x86-64 `Binaries/Win64/HatinTimeGame.exe` (48,095 functions, 0 lift errors) → C (`pcrecomp` `generate64`) →
clang/NDK, Win64 runtime (`runtime/src/`), Direct3D 9 over DXVK Native 1.9.2b (`gfx/dxvk/`, `gfx/hat_d3d9.cpp`)
on Vulkan 1.1, native XAudio2.7 + `stb_vorbis` audio on SDL3, native C LZO1X override (`0x140001050`).
No Wine, no Box64, no emulator.

Status and evidence ledger: [`port/RECOMP_STATUS.md`](port/RECOMP_STATUS.md).
Never commit game files, `ref/`, `gen/`, `build/`, `work/`, `out/`, APKs, `.so` files, or keystores.

## Build Pipeline (Docker image: `hatintime-build` from `docker/Dockerfile`)
- One-shot APK build: `./build.sh "/path/to/A Hat in Time"` (calls `tools/build.sh` inside Docker)
- Fetch third-party deps: `sh tools/fetch_deps.sh`
- Catalog & lift: `python3 tools/pdata_catalog.py ref/lift/HatinTimeGame.exe work/cat/HatinTimeGame.txt && sh tools/lift_module.sh HatinTimeGame`
- DXVK D3D9 (host / Android): `sh tools/build_dxvk.sh host` (Android CMake builds DXVK directly via `add_subdirectory`)
- Host harness: `JOBS=14 sh tools/build_host.sh` → `build/host/hathost <game dir> [user dir]`
- Host headless graphics test (`Xvfb` + `lavapipe`): `sh tools/hostrun_gfx.sh boot 60`
- Android `.so` + APK: `JOBS=14 sh tools/build_android_so.sh` → `cd android && ./gradlew --no-daemon assembleRelease`

## Debug Switches (env on host, or `adb shell setprop debug.hp.env "K=V;..."` on Android)
- `W64_TRACE=1|2|3` — Win64 import call trace
- `W64_FILES=1` — log every `CreateFileW`
- `W64_EH_TRACE=1` — trace Win64 SEH / C++ exceptions
- `HP_STUTTER=1` — per-150-frame frame-time percentiles (`p50`/`p95`/`p99`/`max`), buckets, and DXVK pipeline compile counts
- `HP_DXVK_LOG=0..4` — DXVK log verbosity (default 3 = warn/err)
- `HAT_LZO_VERIFY=1` — one-shot byte-for-byte self-check of native LZO1X (`0x140001050`) against lifted `L_000140001050`
- `W64_NO_OVERRIDES=1` — disable host function overrides (`runtime/src/lzo1x_native.c`)
- `W64_ARGS="..."` — override guest command line (default `-NOSPLASH -WINDOWED -ResX=<w> -ResY=<h>`)
