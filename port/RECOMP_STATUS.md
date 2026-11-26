# A Hat in Time (UE3 Win64, Direct3D 9 SM3) → Android — static recompilation status

| Milestone | State |
|---|---|
| M0 analysis | done 2026-11-25: Unreal Engine 3 (v12097), single Win64 PE module `Binaries/Win64/HatinTimeGame.exe` (20.4 MB, `12.1 MB` `.text`, `tls=False`), Direct3D 9 SM3 (`RefShaderCache-PC-D3D-SM3.upk`), XAudio2.7 + Ogg Vorbis, PhysX statically linked |
| M1 lift | done 2026-11-25: `48,095` functions across 126 generated C units (`53,386` pdata entries), `0` lift errors, `272` todo sites (`recomp_funcs_0000` & `recomp_funcs_0035` in `config/o0_host.txt`) |
| M2 + M3 host boot & D3D9 graphics | done 2026-11-26: 463/463 Win64 imports bound (`0 not implemented`), DXVK Native 1.9.2b D3D9 (`libhp_dxvk.a`), `Xvfb` + `lavapipe` 1280×720 renders Spaceship & Mafia Town 3D title screen cleanly; Vorbis/XAudio2 audio verified (`peak 0.469`); native LZO1X override (`0x140001050`) byte-for-byte verified (`PASS`) |
| M4 Android (`0.1.0`, `ch.plum.hatintime`) | verified playable on **Odin 3** (Snapdragon 8 Elite, Adreno 830, 1280×720): **locked 59.6–60.0 fps** across Gears for Breakfast intro, 3D title screen (incl. Seal the Deal & Nyakuza Metro), file select, and Spaceship 3D gameplay (`p50=16.6–17.1 ms`, `p99=18.5–27.2 ms`, `0` frames ≥30 ms steady-state), SDL3 48 kHz stereo audio (`peak 0.125–0.571`), walking/jumping/camera controls, save file creation (`slot1.hat`), ZIP64 auto-unpack (`7.84 GB` in <10 s), and clean background/resume (`vk_pipeline.cache` 13.86 MB, same PID, instant 59.9 fps resume). Memory: `2.71 GB` PSS in title/menu, `4.53 GB` PSS in Spaceship |

## Key Engineering Highlights & Fixes

1. **DXVK Native 1.9.2b Direct3D 9 backend (`gfx/dxvk/`, `gfx/hat_d3d9.cpp`, `runtime/src/native_call.c`):**
   - Built `src/d3d9` + `src/dxso` + 54 built-in Vulkan 1.1 D3D9 shaders into `libhp_dxvk.a`.
   - Adapted `gfx/dxvk/prepare.py` for Android D3D9 swapchain (`VK_SUBOPTIMAL_KHR`, surface generation tracking, optional Adreno feature gating in `d3d9_device.cpp`), persistent Adreno `VkPipelineCache` (`vk_pipeline.cache`), and lock-free `dxvk-async` pipeline compilation (`23` pipelines / `708 ms` compiled in background during Spaceship load with `max=18.4 ms` frame time).
   - **AArch64 C++ member-pointer ABI fix (`gfx/hat_d3d9.cpp` `vidx()`):** On AArch64 Itanium ABI, the virtual-function flag lives in the second word (`u.b & 1`, offset `u.a / sizeof(void*)`) rather than `u.a & 1`. Fixing this restored `Clear` (float `Z`), `SetNPatchMode`/`GetNPatchMode`, `CreateDevice`, `Reset`, and `Present` interception on ARM64.
2. **Audio — XAudio2.7 + Ogg Vorbis (`runtime/src/xaudio2.c`, `runtime/src/vorbis.c`):**
   - Native XAudio2.7 COM (`IXAudio2`, mastering/submix/source voices) mixing 48 kHz stereo into SDL3 (`w64_audio_sink_*`), plus `X3DAudio1_7` / `XAPOFX1_5`.
   - Fixed UE3's `OggSeekCallback` quirk (`L_0001403D26C0` returns the new byte offset rather than `0`, so `ov_open_callbacks` must always seek back to `cur`), and moved handle storage to a mutex-protected host lookup table (`vf -> ov_wrap_t*`) because UE3 passes `ds == vf`.
3. **Native LZO1X Decompression Override (`runtime/src/lzo1x_native.c`, `config/keep_dispatch.txt`):**
   - Replaced UE3's 30k-line lifted `lzo1x_decompress_safe` (`0x140001050`, compiled `-O0`) with a ~120-line native C `lzo1x_decompress_safe`, verified byte-for-byte against `L_000140001050` (`HAT_LZO_VERIFY=1` -> `PASS`), cutting startup time from ~49 s to ~10 s.
4. **Win64 / UE3 Runtime (`runtime/src/ucrt.c`, `hat_win32.c`, `steam.c`, `xinput.c`, `k32_file.c`):**
   - Full UCRT + `VCRUNTIME140` (wired to `es3_eh_throw`) + `MSVCP140` + `tbbmalloc` + offline Steam stub (AppID `253230`) + dummy `IDirectInput8W` COM object + `WSOCK32` ordinals `#1`–`#116` + `XINPUT1_3` ordinals `#2`/`#3`/`#100`.
   - `chdir("<gamedir>/Binaries/Win64")` + `w64_host_path` relative-path normalization for UE3's `..\..\HatinTimeGame\CookedPC` lookups.
5. **Android App (`ch.plum.hatintime` 0.1.0, `versionCode 1`):**
   - Pure-Java `FileChannel` ZIP/ZIP64 importer (`GameImporter.java`) with `Binaries/Win64/HatinTimeGame.exe` SHA-256 verification (`0197f98641c2...`), weighted 3-button `Launcher.java`, `HatActivity.java` (default `1280×720`, full-res toggle), `PadView.java` (XInput pad + right-half camera drag + `Pad ✕` toggle), and synchronous `SDL_AddEventWatch` background/resume lifecycle.
