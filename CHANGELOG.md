# Changelog

## 0.1.0 — 2026-11-26
- First Android arm64 release (`ch.plum.hatintime`): static recompilation of `Binaries/Win64/HatinTimeGame.exe` (48,095 functions across 126 generated C units, 0 lift errors).
- Direct3D 9 SM3 backend over DXVK Native 1.9.2b (Vulkan 1.1) with persistent Adreno `VkPipelineCache`, lock-free `dxvk-async` pipeline compilation, and AArch64 C++ member-pointer ABI support for `Clear`/`Present`/`CreateDevice`/`Reset`.
- Native XAudio2.7 COM mixer + `stb_vorbis` Ogg Vorbis audio over SDL3 (48 kHz stereo), handling UE3's `OggSeekCallback` offset return value and `ds == vf` aliasing.
- Native C `lzo1x_decompress_safe` override (`0x140001050`), verified byte-for-byte against the lifted version (~5× faster startup and map loads).
- Physical gamepad support (XInput via SDL3), on-screen `PadView` (left stick, A/B/X/Y, LB/RB/LT/RT, Back/Start, right-half camera drag, `Pad ✕` toggle), and touch menu support.
- Pure-Java `FileChannel` ZIP/ZIP64 and SAF folder importer with SHA-256 verification of `Binaries/Win64/HatinTimeGame.exe`, plus synchronous Android background/resume lifecycle.
- Verified on AYN Odin 3 (Snapdragon 8 Elite / Adreno 830, 1280×720): steady 59.6–60.0 fps across intro, 3D title screen (incl. Seal the Deal & Nyakuza Metro), file select, and Spaceship gameplay (`p50 = 16.6–17.1 ms`, `0` frames ≥ 30 ms steady-state).
