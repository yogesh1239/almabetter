# Contra III: The Alien Wars — Recompiled

A native PC port of *Contra III: The Alien Wars* (SNES, USA) built with the
[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) static
recompiler. **No ROM or ROM-derived code is in this repository**: the C
translation of the game (`src/gen/`) is generated locally from your own ROM.

## What this port adds

- **Re-entrant NMI frame driver** (`src/contra_frame_driver.c`). Contra III
  runs its whole game loop inside the NMI handler and, mid-frame, waits for
  the *next* NMI inside that handler. The stock driver runs interrupt handlers
  to RTI with no field deadline, so the wait spun into the interpreter's step
  cap, the handler context was discarded and the game froze on a black
  screen. This driver pushes the NMI frame and keeps running the whole
  program against the field deadline, so a nested NMI releases the wait
  exactly as on hardware.
- **Idle parking.** The two polls that only an interrupt can satisfy (the
  main thread's job poll at `$00:8166` and the in-NMI wait at `$00:85EC`)
  cost ~45% of all interpreted instructions. At those PCs, with the poll
  condition unmet, the CPU is parked like `WAI` while the beam, APU, HDMA and
  raster IRQs run on. A/B over 2,500 frames of scripted play: every WRAM game
  variable identical (only stale bytes below the stack pointer differ).
- **High-refresh interpolation** (`src/contra_interp.c`). The game still
  simulates at 60.0988 Hz. Presentation runs at the display rate, and every
  in-between picture is rasterized again by the PPU with per-scanline BG
  scroll, Mode 7 parameters and each OAM sprite moved to its interpolated
  position (no frame blending). Jumps larger than a plausible one-frame
  motion snap instead of sweeping. Latency cost: at most one field.
- **Internal resolution, auto or 1–4×.** Auto picks the scale that covers the
  screen (4× = 1024×896 at 1080p, 1440p and 4K) and the GPU filters it to
  full screen (`LinearFiltering = 1`, desktop fullscreen and VSync are the
  shipped defaults). The frame is rasterized once per sub-pixel sample phase
  and interleaved: an exact point-sampled N× render of layers and sprites at
  fractional positions, so interpolated motion is sub-pixel smooth. Mode 7
  scanlines use the framework's HD Mode 7 sampler. Passes run on a worker
  pool.
- **Quality governor** (`AutoQuality = 1`). The game must never lose its
  60 Hz to the presenter: from measured guest-frame and present costs it
  steps down (fewer sub-pixel phases → whole-pixel interpolation → 120 Hz
  cap → no interpolation → native output) and back up when there is
  headroom. Pinned to one core at 4×/360 Hz, the game held 59.8 fps.
- **Android** (`android/`): SDL3 app with a first-run ROM picker (system file
  chooser; size and CRC32 checked, copier headers stripped), an on-screen
  multi-touch SNES pad that hides when a gamepad is connected, landscape,
  arm64-v8a / armeabi-v7a / x86_64.

Settings live in `contra3.ini` beside the executable (template in
`assets/dist/`): `Interpolation`, `InternalResolution` (0 = auto),
`PresentationHz`, `AutoQuality`; the environment variables `CONTRA_INTERP`,
`CONTRA_RES`, `CONTRA_FPS`, `CONTRA_AUTOQ` override them.

### Ahead-of-time code: not enabled, on purpose

`tools/contra/safe_roots.py` selects observed call targets whose whole static
call tree avoids the vblank wait, the idle loop, indirect jumps and unknown
widths, and promoting those 114 routines builds and runs. But compared with
the interpreter its WRAM drifts from the intro onward (consistent with
different cycle accounting moving where NMI lands), and the gain was ~12%
CPU, so the shipped build stays on the exact interpreter + idle parking.

## Build

Needs Git, Python 3.9+, Rust 1.85+, CMake, Ninja and a C/C++ compiler.

```sh
scripts/fetch_framework.sh
bash tools/regen.sh --rom "/path/to/Contra III - The Alien Wars (USA).sfc"

# Linux (system SDL2)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_SDL_BACKEND=SDL2
cmake --build build

# Windows x64, cross-compiled from Linux with mingw-w64 (SDL3 built in, one exe)
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw64-toolchain.cmake
cmake --build build-win

# Android APK (Android SDK with platform 35, build-tools 35, NDK 27.2,
# CMake 3.22.1; Gradle 8.9+). Output: android/app/build/outputs/apk/release/
cd android && echo "sdk.dir=$ANDROID_HOME" > local.properties && gradle assembleRelease
```

Run with the ROM next to the executable, named
`Contra III - The Alien Wars (USA).sfc` (CRC32 `84da7cfe`), or pass its path.

## Status

Alpha. Verified: boot, Konami logo, intro, title, stage 1 (scripted play) and
the stage 1/3 two-player attract demos, at native and 2×/3×/4× with
interpolation, on Linux. The Windows exe and the Android APK are
cross-compiled from the same sources and have not been run on those systems
here. Not yet played through: later stages, including the Mode 7
top-down stages. Most game code still runs on the framework's interpreter
tier (it holds full speed); naming functions in `recomp/symbols.toml` and
regenerating promotes them to ahead-of-time C.

Built binaries contain code translated from the ROM: keep them to yourself.
