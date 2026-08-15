# lr-flycast for Raspberry Pi 3B (RetroPie) - Optimized Build

A fork of `libretro/flycast`, built with a GCC cross-compiler for Raspberry Pi 3B (BCM2837, Cortex-A53, VideoCore IV, 32-bit ARM/armhf) with aggressive code generation flags and a set of patches that eliminate key bottlenecks in CPU, GPU, texture cache, and audio subsystems.

---

## What has been changed relative to upstream

### 1. Compilation and microarchitectural tuning (`Makefile`)
* **Code generation flags:** Added `-Ofast`, optimization for the Cortex-A53 microarchitecture (`-mtune=cortex-a53 -mfpu=neon-fp-armv8 -marm -mfloat-abi=hard`), as well as safe instruction graph optimization flags (`-fno-plt -fno-semantic-interposition -fipa-pta -funroll-loops --param max-unroll-times=4 -ftree-vectorize -fomit-frame-pointer`).
* **Exclusions:** **Not used** `-flto` (causes runtime crash with dynamic linking) and `-mcpu=cortex-a53+crypto` (in 32-bit mode on Pi 3B produces `SIGILL`).

### 2. SH4 dynarec and FPU (`core/hw/sh4/dyna/decoder.cpp`, `driver.cpp`)
* **Virtual SH4 underclocking ($200 \to 145$ MHz):** In `decoder.cpp` for the `LOW_END` profile, a multiplier of $1.375\times$ is applied to `guest_cycles`. Blocks consume 37.5% more virtual time, reducing the volume of compiled and executed machine code by ~27% per frame without slowing down game logic.
* **FPU opcode correction:** Fixed time quantum accounting for vector FPU instructions (`op >= 0xF000`), which previously incorrectly had zero cost (`+0 cycles`), overloading 3D blocks.
* **Hardware Flush-to-Zero (FTZ):** In `driver.cpp` before running JIT code, the `FZ` (Flush-to-Zero) and `DN` (Default NaN) bits are set in hardware in the `FPSCR` register, eliminating Cortex-A53 pipeline micro-pauses when handling denormalized floating-point numbers.

### 3. Audio path and AICA (`core/libretro/audiostream.cpp`, `core/hw/aica/aica.cpp`, `core/hw/arm7/arm7.cpp`)
* **Non-blocking audio stream (`audiostream.cpp`):** Eliminated synchronous sleeping of the emulator thread (`emu_thread`) when transferring samples to RetroArch/ALSA.
* **Batched audio processing (`aica.cpp`):** Force-enabled fast 32-sample rendering (`AICA_Sample32()`) and added early exit for interrupts `if (!p_ints)`.
* **Vectorized `FlushCache` (`arm7.cpp`):** Clearing of the `EntryPoints` table of the ARM7 sound processor rewritten using NEON SIMD, eliminating a scalar loop of 2.1 million iterations (8 MB memory rewrite) that caused 4-8 ms freezes during track changes.

### 4. Texture cache and VRAM (`core/rend/TexCache.cpp`)
* **Per-frame VRAM hash caching:** Implemented a direct `vram_hash_cache` keyed by frame number `FrameCount`. Eliminated repeated idle `XXH32` recalculations over megabytes of video memory for dynamic textures (HUD, fonts, animated sprites).
* **Fast exit on memory write:** In `VramLockedWriteOffset()`, removed redundant `vramlist_lock` mutex acquisitions for unlocked pages.

### 5. OpenGL ES state cache (`core/rend/gles/glcache.h`, `gldraw.cpp`)
* **Direct cache pointer (`glcache.h`):** Current texture parameters are cached via the `_cur_texture_params` pointer in `BindTexture()`. This turned `TexParameteri` calls into $O(1)$ direct accesses, eliminating over 50,000 red-black tree (`std::map`) lookups per frame.
* **State deduplication (`gldraw.cpp`):** In `DrawList`, `SetGPState` is called only when state actually changes between strips (`SameGPState`), reducing VideoCore IV driver overhead.

### 6. Core options and resolutions (`core/libretro/libretro_core_options.h`, `libretro.cpp`)
* Added ultra-low resolutions `256x192` and `200x150` (4:3 aspect ratio).
* By default for the `LOW_END` profile, optimal values are hardcoded:
  * `reicast_volume_modifier_enable = disabled` (complete disabling of the double-pass PowerVR2 stencil shadow pass);
  * `reicast_anisotropic_filtering = disabled`;
  * `reicast_fog = disabled`;
  * `reicast_mipmapping = disabled`;
  * `reicast_threaded_rendering = enabled`.

---

## Build Requirements (Cross-compilation on x86_64 Linux)

1. **armhf toolchain:**
   ```bash
   sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
   ```

2. **VideoCore SDK and system sysroot:**
   * Copy `/opt/vc` and system libraries (`/lib`, `/usr/lib`) from the Raspberry Pi into a local `sysroot/` folder (glibc 2.28+ supported).

3. **Compat objects (for compatibility of modern GCC with glibc 2.28):**
   * `libc_compat.c` - implements a stub for `__libc_single_threaded`.
   * `libc_compat2.cpp` - implements `std::throw_bad_array_new_length`.

   Building the objects:
   ```bash
   arm-linux-gnueabihf-gcc -c libc_compat.c -o libc_compat.o
   arm-linux-gnueabihf-g++ -c libc_compat2.cpp -o libc_compat2.o
   ```

---

## Building

```bash
make clean
make -j$(nproc) ARCH=arm CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ \
     CC_PREFIX=arm-linux-gnueabihf- CC_AS=arm-linux-gnueabihf-gcc \
     platform=rpi3 WITH_DYNAREC=arm HAVE_LTCG=0 HAVE_GL3=0 HAVE_VULKAN=0 GLES=1 \
     GL_LIB="-L/path/to/sysroot/opt/vc/lib -lbrcmGLESv2" \
     LDFLAGS="-L. -l:libc_compat.o -l:libc_compat2.o"
```

Stripping after build (reduces the binary from ~25 MB to ~24 MB):
```bash
arm-linux-gnueabihf-strip flycast_libretro.so
```

---

## Installation on RetroPie

Copy the built file to the Raspberry Pi:
```bash
scp flycast_libretro.so pi@retropie:/opt/retropie/libretrocores/lr-flycast/
```

---

## Recommended settings for maximum smoothness

### 1. System settings (`/opt/retropie/configs/dreamcast/retroarch.cfg`)

```ini
#include "/opt/retropie/configs/all/retroarch.cfg"

# === VIDEO ===
# Disable RetroArch's external video thread to avoid queue delays
# (the internal Flycast core thread is used)
video_threaded = "false"
video_vsync = "true"
video_hard_sync = "false"
video_max_swapchain_images = "2"

# === AUDIO ===
audio_driver = "alsathread"
audio_sync = "true"
audio_rate_control = "true"
# Elastic rate control compensates for micro-desync 59.83 Hz (Dreamcast) vs 59.94 Hz (TV)
audio_rate_control_delta = "0.05"
audio_max_timing_skew = "0.05"
audio_resampler = "linear"
audio_resampler_quality = "1"
audio_out_rate = "44100"
audio_latency = "64"

fps_show = "true"
```

### 2. Core options (`/opt/retropie/configs/all/retroarch/config/Flycast/Flycast.opt`)

```ini
reicast_frame_skipping = "1"
reicast_volume_modifier_enable = "disabled"
reicast_alpha_sorting = "per-strip (fast, least accurate)"
reicast_threaded_rendering = "enabled"
reicast_framerate = "fullspeed"
reicast_fog = "disabled"
reicast_mipmapping = "disabled"
reicast_anisotropic_filtering = "disabled"
reicast_enable_dsp = "disabled"
reicast_cable_type = "VGA (RGB)"
reicast_internal_resolution = "640x480"
reicast_gdrom_fast_loading = "enabled"
```
## Acknowledgements and Upstream

* Original repository: [flyinghead/flycast](https://github.com/flyinghead/flycast)
* Libretro core: [libretro/flycast](https://github.com/libretro/flycast)
* Historical roots: the [Reicast](https://github.com/reicast/reicast-emulator) project and nullDC.
* License: [GPL-2.0](LICENSE)
