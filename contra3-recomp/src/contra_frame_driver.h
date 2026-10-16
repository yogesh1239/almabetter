#ifndef CONTRA_FRAME_DRIVER_H
#define CONTRA_FRAME_DRIVER_H

/* Beam-aligned frame driver with in-stream (re-entrant) NMI delivery.
 * See contra_frame_driver.c. Drawing uses the framework's
 * snes_beam_frame_driver_draw_ppu_frame(). */
void contra_frame_driver_run_frame(void);
void contra_frame_driver_reset(void);

#endif
