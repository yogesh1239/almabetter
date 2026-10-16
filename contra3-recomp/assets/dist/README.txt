Contra III: The Alien Wars - Recompiled (native PC)
===================================================

The game's 65816 code runs natively on the snesrecomp framework (graphics,
sound and DMA come from its SNES hardware runtime). No ROM is included.

SETUP
  Put your own copy of the US ROM next to the executable, named exactly:
      Contra III - The Alien Wars (USA).sfc
  (CRC32 84da7cfe; a copier-headered .smc also works). Or drag the ROM onto
  the executable / pass its path on the command line. The ROM is checked
  against the expected digests at startup.

  It starts in desktop fullscreen at your monitor's resolution (1080p,
  1440p, 4K...). Alt+Enter toggles windowed mode.

ENHANCEMENTS (contra3.ini)
  Interpolation       Frames are re-rendered between the original 60 Hz
                      game frames at your display's refresh rate (120/144/240
                      Hz...). Background scroll, Mode 7 and every sprite are
                      moved to their in-between positions; nothing is blended
                      or smeared. Adds at most one frame (16 ms) of latency.
  InternalResolution  0 = auto: 4x (1024x896) on 1080p/1440p/4K screens,
                      then smoothly scaled to fill them. Or force 1-4. Higher
                      values give sub-pixel smooth motion and HD Mode 7.
                      Pixel art itself is not redrawn.
  PresentationHz      Force a presentation rate instead of the display's.
  AutoQuality         On a slower CPU the enhancement level drops by itself
                      (and comes back when there is headroom) so the game
                      never slows down. 0 = always use the settings above.

  VSync, fullscreen, linear filtering, window size, controls and audio are in
  config.ini and keybinds.ini, written beside the executable on first run.

KNOWN LIMITS
  - Alpha-quality port: verified playing the intro, title, and stages 1 and 3
    (attract demos and scripted play) on Linux. Mode 7 stages were not
    played through. The Windows build is cross-compiled and was not run on
    Windows by the porter.
  - Do not redistribute this build: it contains code translated from the ROM.
