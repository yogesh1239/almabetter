Contra III: The Alien Wars - Recompiled (native PC)
===================================================

The game's 65816 code was statically recompiled to C and built as a native
program on the snesrecomp framework (graphics, sound and DMA come from its
SNES hardware runtime). No ROM is included.

SETUP
  Put your own copy of the US ROM next to the executable, named exactly:
      Contra III - The Alien Wars (USA).sfc
  (CRC32 84da7cfe; a copier-headered .smc also works). Or drag the ROM onto
  the executable / pass its path on the command line. The ROM is checked
  against the expected digests at startup.

ENHANCEMENTS (contra3.ini)
  Interpolation       Frames are re-rendered between the original 60 Hz
                      game frames at your display's refresh rate (120/144/240
                      Hz...). Background scroll, Mode 7 and every sprite are
                      moved to their in-between positions; nothing is blended
                      or smeared. Adds at most one frame (16 ms) of latency.
  InternalResolution  1-4x. The picture is rasterized at N x 256 x 224 with
                      layers and sprites at fractional positions, so motion is
                      sub-pixel smooth; Mode 7 scanlines are sampled in HD.
                      Pixel art itself is not redrawn.
  PresentationHz      Force a presentation rate instead of the display's.

  VSync, window size, fullscreen, controls and audio are in config.ini and
  keybinds.ini, written beside the executable on first run.

KNOWN LIMITS
  - Alpha-quality port: verified playing the intro, title, and stages 1 and 3
    (attract demos and scripted play). Mode 7 stages were not played through.
  - Simulation uses the framework's interpreter tier for most code paths; it
    runs at full speed but is not yet fully ahead-of-time compiled.
  - Do not redistribute this build: it contains code translated from the ROM.
