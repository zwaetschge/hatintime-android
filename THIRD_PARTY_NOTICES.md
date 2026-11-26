# Third-party components

## Included in this repository

| Component | Where | License |
|---|---|---|
| SDL 3.2.12 Android Java glue (`org.libsdl.app`), with small changes for a fixed-size render surface | `android/app/src/main/java/org/libsdl/app/` | zlib (© Sam Lantinga) |
| pcrecomp runtime headers (`cpu64.h`, `eh64.c/.h`), adapted | `runtime/include/` | MIT (© sp00nz) |
| Vulkan Android / Xlib platform header shims | `third_party/dxvk_android_include/` | Apache-2.0 / MIT |
| Gradle wrapper | `android/gradlew`, `android/gradle/wrapper/` | Apache-2.0 (© Gradle Inc.) |

## Fetched at build time (`tools/fetch_deps.sh`) and linked into the APK

| Component | Version | License |
|---|---|---|
| [pcrecomp](https://github.com/sp00nznet/pcrecomp) (lifter, build-time only) | `35548b8` + `tools/patches/pcrecomp-hatintime.patch` | MIT |
| [DXVK Native](https://github.com/Joshua-Ashton/dxvk-native) (Direct3D 9 → Vulkan) | `native-1.9.2b` + `gfx/dxvk/prepare.py` | zlib |
| [SDL](https://github.com/libsdl-org/SDL) | `3.2.12` | zlib |
| [stb_vorbis](https://github.com/nothings/stb) (Ogg Vorbis decoder) | `6b5799b` (v1.22) | MIT or public domain |

The full license texts ship with each fetched source tree in `third_party/`.

## Not included

**A Hat in Time** (© Gears for Breakfast): neither its executable code (`HatinTimeGame.exe`), nor code generated from it (`gen/`), nor its game data (`Engine/`, `HatinTimeGame/`) are part of this repository.
