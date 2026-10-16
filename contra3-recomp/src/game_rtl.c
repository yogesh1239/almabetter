/*
 * Contra III — per-game runtime glue.
 *
 * THIS FILE IS THE PORT. Everything else the scaffold produced is layout,
 * build wiring, and packaging; this is where the actual work happens.
 *
 * The framework does not, and cannot, drive an arbitrary SNES game on its
 * own. Two responsibilities always land on the game host:
 *
 *   1. Deciding what "one frame" means for THIS title, and returning from
 *      run_frame() at that boundary. A real ROM's reset vector never
 *      returns — it enters a main loop that waits on vblank — so somebody
 *      has to choose the yield point.
 *   2. Delivering NMI/IRQ at the hardware edge (see
 *      snesrecomp/docs/FRAME_MODEL_HOSTS.md).
 *
 * A new port starts on the framework's beam-aligned driver
 * (runner/src/beam_frame_driver.h): one frame is one PPU field, cut at the
 * beam's vblank edge; interrupts -- raster splits and a coprocessor's IRQ
 * line -- are taken where the beam latches them; parked time still runs the
 * beam, APU and coprocessors; and the field is rasterized from the raster
 * journal with real HDMA. It is the framework's, so a fix to it reaches this
 * port on a submodule pull.
 *
 * This file used to hold its own copy of a simpler driver: frames measured
 * from wherever the CPU last stopped, NMI delivered at an arbitrary beam
 * position, the field drawn with end-of-frame register state. A game that
 * uploads in vblank between INIDISP=$80 and $0F flickered black, and one
 * that chains raster splits fell apart. Replace these calls only for what
 * the driver does not do; MetalWarriorsSNESRecomp's src/mw_rtl.c and
 * GundamWingEndlessDuelSNESRecomp's src/game_rtl.c are developed examples of
 * title-specific drivers.
 */

#include "game_rtl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "beam_frame_driver.h"
#include "contra_frame_driver.h"
#include "contra_interp.h"
#include "desktop/sdl_compat.h"

extern uint8_t g_ram[0x20000];

void GameRunOneFrame(void)
{
    const uint64_t t0 = SDL_GetPerformanceCounter();
    contra_frame_driver_run_frame();
    contra_interp_note_sim_ms(1000.0 * (double)(SDL_GetPerformanceCounter() - t0) /
                              (double)SDL_GetPerformanceFrequency());
    /* CONTRA_RAM_DUMP_DIR=<dir>: WRAM every 100 frames, for A/B validation
     * of frame-driver changes. */
    static const char *dump_dir;
    static int checked, frame;
    if (!checked) {
        checked = 1;
        dump_dir = getenv("CONTRA_RAM_DUMP_DIR");
    }
    if (dump_dir && (++frame % 100) == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/wram_%06d.bin", dump_dir, frame);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(g_ram, 1, sizeof g_ram, f);
            fclose(f);
        }
    }
}

void GameDrawPpuFrame(void)
{
    contra_interp_draw_ppu_frame();
}

void GameSessionReset(void)
{
    /* Rematch / soft-return: clear anything that must not survive a new
     * session. recomp-ai-rules/NETPLAY.md §3 — sticky state that "has always
     * been fine" is the usual desync culprit, because single-player never
     * re-enters the boot path twice in one process. */
    contra_frame_driver_reset();
    contra_interp_reset();
}

const RtlGameInfo kGameInfo = {
    .title = "contraiii",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
    .session_reset = &GameSessionReset,
};
