# A Hat in Time – Static Recompilation for Android

Static x86-64 → C → ARM64 recompilation of **A Hat in Time** (Unreal Engine 3, Win64, Direct3D 9 SM3) for Android (`ch.plum.hatintime`).

No x86 emulator, Wine, or Box64 is used: `Binaries/Win64/HatinTimeGame.exe` (48,095 functions across 126 generated C units) is lifted ahead of time with `pcrecomp` and linked directly against a native Win64 runtime, DXVK Native 1.9.2b (Direct3D 9 → Vulkan 1.1), native XAudio2 + `stb_vorbis` audio, native LZO1X package decompression, and SDL3 (windowing, touch, gamepad, audio).

## Repository Contents

- `runtime/` — Win64 runtime (`core.c`, `ucrt.c`, `hat_win32.c`, `steam.c`, `xaudio2.c`, `vorbis.c`, `lzo1x_native.c`, `xinput.c`, `native_call.c`, `user32.c`, `k32_*.c`)
- `gfx/` — Direct3D 9 COM bridge (`hat_d3d9.cpp`) and DXVK Native 1.9.2b Android/Vulkan patches (`gfx/dxvk/`)
- `host/` — Linux x86-64 debug harness (`hathost`) for headless `Xvfb` + `lavapipe` verification
- `android/` — Android app (`ch.plum.hatintime`): `Launcher` (SAF folder & ZIP/ZIP64 `FileChannel` importer), `HatActivity` (SDL3), `PadView` (on-screen XInput gamepad + camera drag)
- `tools/` — Lift, DXVK, host, and Android arm64 build scripts (`lift_module.sh`, `build_dxvk.sh`, `build_host.sh`, `build_android_so.sh`, `make_icon.py`)
- `config/` — `o0_host.txt` (large-function `-O0` list) and `keep_dispatch.txt` (`0x140001050` LZO1X override)

No copyrighted game assets, lifted C files, binaries, or keystores are included in this repository.

## Building (with your own PC copy of A Hat in Time)

1. Copy `Binaries/Win64/HatinTimeGame.exe` into `ref/lift/` and generate `ref/lift/layout.json` (`0x140000000`).
2. Lift the binary: `tools/lift_module.sh HatinTimeGame`
3. Build DXVK Native (D3D9): `tools/build_dxvk.sh host`
4. Build host harness (optional): `tools/build_host.sh`
5. Build Android arm64 `.so` + APK: `tools/build_android_so.sh` and `cd android && ./gradlew assembleRelease`
