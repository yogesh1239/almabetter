#ifndef CONTRA_INTERP_H
#define CONTRA_INTERP_H

#include <stddef.h>
#include <stdint.h>

/* High-refresh interpolation and internal resolution: see contra_interp.c. */
void contra_interp_draw_ppu_frame(void);
void contra_interp_reset(void);
double contra_interp_presentation_hz(double display_refresh);
int contra_interp_render_scale(void);
int contra_interp_draw_frame(uint8_t *dst, size_t pitch, const uint8_t *field,
                             int frame_w, int frame_h, double alpha);

#endif
