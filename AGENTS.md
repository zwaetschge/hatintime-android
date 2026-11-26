# Project: hatintime-android

Static recompilation of **A Hat in Time** (PC Win64, Unreal Engine 3, Direct3D 9) for Android arm64.
x86-64 `HatinTimeGame.exe` (48,095 functions, 0 lift errors) → C (`pcrecomp` `generate64`) → clang/NDK,
own Win64 runtime (`runtime/src`), Direct3D 9 through DXVK native 1.9.2b (`src/d3d9` + `src/dxso` + `src/dxvk`)
on Vulkan, native XAudio2.7 + `stb_vorbis` audio on SDL3. No Wine/Box64/emulator.

Status/evidence ledger: `port/RECOMP_STATUS.md`. Never commit game files, `ref/`, `gen/`, `build/`, `work/`,
APKs, `.so` files, or keystores.

## Pipeline (Docker image: `hp-gfx:1` mounted at `/srv/plum/projects/hatintime-android:/w -w /w`)
- Lift: `python3 tools/pdata_catalog.py ref/lift/HatinTimeGame.exe work/cat/HatinTimeGame.txt && bash tools/lift_module.sh HatinTimeGame`
- DXVK D3D9: `bash tools/build_dxvk.sh host|android`
- Host: `JOBS=16 bash tools/build_host.sh` → `build/host/hathost <game dir> [user dir]`
- Android: `JOBS=16 bash tools/build_android_so.sh` → `android/gradlew assembleRelease` (debug keystore mounted at `/root/.android/debug.keystore`)
