/*
 * Contra III — presentation: high-refresh interpolation and internal
 * resolution, both done by re-rasterizing with the real PPU.
 *
 * The guest still simulates at the SNES field rate (60.0988 Hz); nothing here
 * touches game logic, RAM or timing. The desktop host decouples presentation
 * (presentation_hz hook) and asks draw_frame() for pictures between simulated
 * fields with a weight `alpha` in [0,1]. Each picture is rasterized again
 * from the newest field's VRAM/CGRAM/registers with motion moved back in time
 * to field(N-1) + alpha * (field(N) - field(N-1)):
 *
 *   - BG1-4 H/V scroll, per scanline (raster splits and HDMA parallax
 *     interpolate line by line),
 *   - Mode 7 matrix, center and scroll, per scanline,
 *   - every OAM sprite's position, matched to its counterpart in the previous
 *     field by slot and tile.
 *
 * A value that jumps further than a plausible single-field motion (a scene
 * cut, a teleport, a slot reused by another object) snaps to the newest
 * value. Presentation trails the simulation by at most one field (~16.6 ms),
 * the usual cost of interpolation.
 *
 * Internal resolution N (2..4) renders an N*256 x N*224 picture. Tile art is
 * not invented; what N buys is POSITION precision: interpolated scroll and
 * sprite positions are fractional, so the frame is rasterized N*N times, once
 * per sub-pixel sample phase, each pass snapping every layer and sprite to
 * the integer offset that the sample at that phase lands on, and the passes
 * are interleaved. The result is exactly a point-sampled N x render of
 * layers and sprites at fractional positions. Mode 7 scanlines instead come
 * from the framework's HD Mode 7 sampler, which evaluates the affine
 * transform at every high-resolution pixel.
 *
 * Rasterizing happens on a private copy of the PPU, so the guest-visible PPU
 * (g_ppu: $213E flags, latches, buffers) is never modified by a present.
 *
 * Settings: contra3.ini beside the executable, overridden by the environment.
 *   [Video]
 *   Interpolation = 1        ; CONTRA_INTERP      0 = present at 60 Hz
 *   InternalResolution = 2   ; CONTRA_RES         1..4
 *   PresentationHz = 0       ; CONTRA_FPS         0 = display refresh rate
 */
#include "contra_interp.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "beam_frame_driver.h"
#include "desktop/sdl_compat.h"
#include "snes/ppu.h"

extern Ppu *g_ppu;

enum {
  kLines = 225,            /* observer lines are 1..224 */
  kMaxScale = 4,
  kMaxScrollStep = 48,     /* px per field; beyond this a layer snaps */
  kMaxM7Step = 64,         /* Mode 7 center/scroll, px per field */
  kMaxM7MatrixStep = 0x60, /* 8.8 fixed point per field */
  kMaxSpriteStep = 40,     /* px per field */
};

/* The PPU's display-register block: everything from INIDISP through CGWSEL
 * (mode, scroll, Mode 7, windows, color math, fixed color). The struct lays
 * these out first, ahead of the port latches. */
#define CONTRA_PPU_REG_BYTES offsetof(Ppu, vramPointer)

/* Per-scanline state as the PPU saw it right before drawing the line (after
 * the raster journal and HDMA applied their writes): the full register block
 * and palette, replayed when re-rasterizing, plus the motion values pulled
 * out for interpolation. */
typedef struct LineMotion {
  uint8_t regs[CONTRA_PPU_REG_BYTES];
  uint16_t cgram[0x100];
  uint16_t hScroll[4], vScroll[4];
  int16_t m7matrix[8];
  uint8_t bgmode;
  uint8_t inidisp;
} LineMotion;

typedef struct FieldMotion {
  bool valid;
  unsigned number;
  LineMotion line[kLines];
  uint16_t oam[0x100];
  uint8_t highOam[0x20];
} FieldMotion;

/* One line's motion at the presented instant, before sub-pixel snapping. */
typedef struct LinePose {
  double h[4], v[4]; /* BG scroll, unwrapped (wrapped to 10 bits on use) */
  int16_t m7matrix[8];
} LinePose;

static FieldMotion s_fields[2];
static int s_cur; /* index of the newest field in s_fields */
static unsigned s_draw_number;

static struct {
  bool loaded;
  int interp;
  int scale;
  double hz;
} s_cfg;

/* A rasterizer: a private PPU copy and one native pass buffer. The calling
 * thread owns worker 0; the rest belong to the pool below. */
typedef struct Raster {
  Ppu *ppu;
  uint8_t *pass; /* 256 x 224 */
} Raster;

enum { kMaxWorkers = 16 };
static Raster s_raster[kMaxWorkers];
static uint32_t *s_hd;    /* HD Mode 7 surface, 256*N x 224*N */
static LinePose s_pose[kLines];
static double s_sprite_x[128], s_sprite_y[128];

/* ── settings ────────────────────────────────────────────────────────────── */

static void cfg_set(const char *key, const char *val) {
  if (!strcmp(key, "interpolation")) s_cfg.interp = atoi(val) != 0;
  else if (!strcmp(key, "internalresolution")) s_cfg.scale = atoi(val);
  else if (!strcmp(key, "presentationhz")) s_cfg.hz = atof(val);
}

static void cfg_load_ini(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char *c = strpbrk(line, ";#");
    if (c) *c = 0;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char key[64] = {0}, val[64] = {0};
    int k = 0;
    for (char *p = line; *p && k < 63; p++)
      if (!isspace((unsigned char)*p)) key[k++] = (char)tolower((unsigned char)*p);
    sscanf(eq + 1, " %63s", val);
    cfg_set(key, val);
  }
  fclose(f);
}

static void cfg_load(void) {
  if (s_cfg.loaded)
    return;
  s_cfg.loaded = true;
  s_cfg.interp = 1;
  s_cfg.scale = 2;
  s_cfg.hz = 0;
  char *base = (char *)SDL_GetBasePath();
  if (base) {
    char path[1024];
    snprintf(path, sizeof path, "%scontra3.ini", base);
    cfg_load_ini(path);
#if !SNESRECOMP_SDL3
    SDL_free(base);
#endif
  }
  const char *v;
  if ((v = getenv("CONTRA_INTERP"))) cfg_set("interpolation", v);
  if ((v = getenv("CONTRA_RES"))) cfg_set("internalresolution", v);
  if ((v = getenv("CONTRA_FPS"))) cfg_set("presentationhz", v);
  if (s_cfg.scale < 1) s_cfg.scale = 1;
  if (s_cfg.scale > kMaxScale) s_cfg.scale = kMaxScale;
  fprintf(stderr, "[contra3] interpolation=%d internal_resolution=%dx presentation_hz=%s\n",
          s_cfg.interp, s_cfg.scale, s_cfg.hz > 0 ? "fixed" : "display");
}

static bool enhanced(void) {
  cfg_load();
  return s_cfg.interp || s_cfg.scale > 1;
}

/* ── capture ─────────────────────────────────────────────────────────────── */

static void observe_line(const struct Ppu *ppu, unsigned line, void *context) {
  FieldMotion *f = (FieldMotion *)context;
  if (line >= kLines)
    return;
  LineMotion *m = &f->line[line];
  memcpy(m->regs, ppu, sizeof m->regs);
  memcpy(m->cgram, ppu->cgram, sizeof m->cgram);
  memcpy(m->hScroll, ppu->hScroll, sizeof m->hScroll);
  memcpy(m->vScroll, ppu->vScroll, sizeof m->vScroll);
  memcpy(m->m7matrix, ppu->m7matrix, sizeof m->m7matrix);
  m->bgmode = ppu->bgmode;
  m->inidisp = ppu->inidisp;
}

void contra_interp_draw_ppu_frame(void) {
  if (!enhanced()) {
    snes_beam_frame_driver_draw_ppu_frame();
    return;
  }
  int next = s_cur ^ 1;
  FieldMotion *f = &s_fields[next];
  f->valid = false;
  snes_beam_frame_driver_draw_ppu_frame_observed(observe_line, f);
  memcpy(f->oam, g_ppu->oam, sizeof f->oam);
  memcpy(f->highOam, g_ppu->highOam, sizeof f->highOam);
  f->number = ++s_draw_number;
  f->valid = true;
  s_cur = next;
}

void contra_interp_reset(void) {
  s_fields[0].valid = s_fields[1].valid = false;
}

/* ── motion at the presented instant ─────────────────────────────────────── */

/* Signed shortest delta on a ring of `bits` bits. */
static int ring_delta(int from, int to, int bits) {
  int mask = (1 << bits) - 1, half = 1 << (bits - 1);
  return ((to - from + half) & mask) - half;
}

/* A ring-valued register at weight a, unwrapped; snaps to `to` on a jump. */
static double lerp_ring(int from, int to, int bits, int max_step, double a) {
  int d = ring_delta(from, to, bits);
  if (abs(d) > max_step)
    return to;
  return from + d * a;
}

static void pose_line(LinePose *out, const LineMotion *p, const LineMotion *c,
                      double a) {
  /* A line whose mode or forced blank flipped between fields belongs to a
   * different scene region: keep the newest field's state. */
  bool same = p->bgmode == c->bgmode &&
              (p->inidisp & 0x80) == (c->inidisp & 0x80);
  if (a >= 1)
    same = false;
  for (int i = 0; i < 4; i++) {
    int ph = p->hScroll[i] & 0x3ff, ch = c->hScroll[i] & 0x3ff;
    int pv = p->vScroll[i] & 0x3ff, cv = c->vScroll[i] & 0x3ff;
    out->h[i] = same ? lerp_ring(ph, ch, 10, kMaxScrollStep, a) : ch;
    out->v[i] = same ? lerp_ring(pv, cv, 10, kMaxScrollStep, a) : cv;
  }
  memcpy(out->m7matrix, c->m7matrix, sizeof out->m7matrix);
  if (same && (c->bgmode & 7) == 7) {
    bool snap = false;
    for (int k = 0; k < 4; k++)
      if (abs((int)c->m7matrix[k] - (int)p->m7matrix[k]) > kMaxM7MatrixStep)
        snap = true;
    if (!snap)
      for (int k = 0; k < 4; k++)
        out->m7matrix[k] = (int16_t)lrint(
            p->m7matrix[k] + (c->m7matrix[k] - p->m7matrix[k]) * a);
    for (int k = 4; k < 8; k++)
      out->m7matrix[k] = (int16_t)((int)lrint(lerp_ring(
          (uint16_t)p->m7matrix[k] & 0x1fff, (uint16_t)c->m7matrix[k] & 0x1fff,
          13, kMaxM7Step, a)) & 0x1fff);
  }
}

typedef struct OamSprite {
  int x, y;       /* x: 9-bit signed (-256..255); y: 8-bit ring */
  uint16_t chr;   /* tile + attributes word */
  uint8_t big;
} OamSprite;

static OamSprite oam_get(const uint16_t *oam, const uint8_t *high, int i) {
  OamSprite s;
  int bits = high[i >> 2] >> ((i & 3) * 2);
  int x = (oam[i * 2] & 0xff) | ((bits & 1) << 8);
  s.x = x >= 256 ? x - 512 : x;
  s.y = oam[i * 2] >> 8;
  s.chr = oam[i * 2 + 1];
  s.big = (bits >> 1) & 1;
  return s;
}

static void oam_set_pos(uint16_t *oam, uint8_t *high, int i, int x, int y) {
  x &= 0x1ff;
  oam[i * 2] = (uint16_t)((x & 0xff) | ((y & 0xff) << 8));
  int shift = (i & 3) * 2;
  high[i >> 2] = (uint8_t)((high[i >> 2] & ~(1 << shift)) | ((x >> 8) << shift));
}

/* Sprites parked off the visible field (the usual "hide" Y values). */
static bool oam_hidden(const OamSprite *s) {
  return s->y >= 224 && s->y < 240;
}

/* Fractional position of every sprite at weight a. Returns how many moved. */
static int pose_oam(const FieldMotion *p, const FieldMotion *c, double a,
                    bool interp) {
  OamSprite prev[128];
  for (int j = 0; j < 128; j++)
    prev[j] = oam_get(p->oam, p->highOam, j);

  int moved = 0;
  for (int i = 0; i < 128; i++) {
    OamSprite s = oam_get(c->oam, c->highOam, i);
    s_sprite_x[i] = s.x;
    s_sprite_y[i] = s.y;
    if (!interp || a >= 1 || oam_hidden(&s))
      continue;
    /* Best counterpart in the previous field: same slot and same tile is
     * the common case; an object list that shifted moves the same tile to
     * another slot; an animating sprite keeps its slot but changes tile.
     * Anything else (a slot reused by an unrelated object) does not
     * interpolate. */
    int best = -1, best_score = 1 << 30, bdx = 0, bdy = 0;
    for (int j = 0; j < 128; j++) {
      const OamSprite *q = &prev[j];
      if (q->big != s.big || oam_hidden(q))
        continue;
      int dx = s.x - q->x;
      int dy = ring_delta(q->y, s.y, 8);
      if (abs(dx) > kMaxSpriteStep || abs(dy) > kMaxSpriteStep)
        continue;
      bool same_chr = q->chr == s.chr, same_slot = j == i;
      if (!same_chr && !same_slot)
        continue;
      int score = (same_chr ? 0 : 1000) + (same_slot ? 0 : 100) +
                  abs(dx) + abs(dy);
      if (score < best_score) {
        best_score = score;
        best = j;
        bdx = dx;
        bdy = dy;
      }
    }
    if (best < 0 || (bdx == 0 && bdy == 0))
      continue;
    s_sprite_x[i] = prev[best].x + bdx * a;
    s_sprite_y[i] = prev[best].y + bdy * a;
    moved++;
  }
  return moved;
}

/* ── rasterization ───────────────────────────────────────────────────────── */

/* Integer offset a layer is drawn at so that the sample at sub-pixel phase
 * `f` (0..1, the sample's position inside a native pixel) of a native pixel
 * hits floor(x + f + s): a point-sampled layer at fractional scroll s. */
static int snap_scroll(double s, double f) { return (int)floor(s + f); }
/* A sprite at fractional position p, sampled at phase f, covers the native
 * pixel x when floor(x + f - p) is inside it: draw it at -floor(f - p). */
static int snap_sprite(double p, double f) { return -(int)floor(f - p); }

static bool raster_ready(Raster *r) {
  if (!r->ppu) {
    r->ppu = (Ppu *)malloc(sizeof(Ppu));
    r->pass = (uint8_t *)calloc(224, 256 * 4);
  }
  return r->ppu && r->pass;
}

/* One native-resolution pass with every layer and sprite snapped for the
 * sample phase (fx, fy). `hd_scale` > 1 also fills the HD Mode 7 surface. */
static void render_pass(Raster *r, const FieldMotion *c, double fx, double fy,
                        int hd_scale) {
  Ppu *scratch = r->ppu;
  memcpy(scratch, g_ppu, sizeof(Ppu));
  scratch->renderBuffer = r->pass;
  scratch->renderPitch = 256 * 4;
  memset(&scratch->mode7Hd, 0, sizeof scratch->mode7Hd);
  for (int k = 0; k < kPpuOverlaySource_Count; k++)
    scratch->overlayRenderBuffer[k] = NULL;
  scratch->widescreenLineEnhancer = NULL;
  if (hd_scale > 1)
    PpuBindMode7HdSurface(scratch, s_hd,
                          (size_t)256 * hd_scale * 4 * 224 * hd_scale,
                          (size_t)256 * hd_scale * 4, 256, 224,
                          (unsigned)hd_scale);

  for (int i = 0; i < 128; i++)
    oam_set_pos(scratch->oam, scratch->highOam, i,
                snap_sprite(s_sprite_x[i], fx), snap_sprite(s_sprite_y[i], fy));

  for (int line = 1; line <= 224; line++) {
    const LineMotion *cl = &c->line[line];
    const LinePose *pose = &s_pose[line];
    /* Replay the newest field's per-line state, then move it in time. */
    memcpy(scratch, cl->regs, sizeof cl->regs);
    memcpy(scratch->cgram, cl->cgram, sizeof cl->cgram);
    for (int i = 0; i < 4; i++) {
      scratch->hScroll[i] = (uint16_t)((snap_scroll(pose->h[i], fx) & 0x3ff) |
                                         (cl->hScroll[i] & ~0x3ff));
      scratch->vScroll[i] = (uint16_t)((snap_scroll(pose->v[i], fy) & 0x3ff) |
                                         (cl->vScroll[i] & ~0x3ff));
    }
    memcpy(scratch->m7matrix, pose->m7matrix, sizeof pose->m7matrix);
    ppu_runLine(scratch, line);
  }
}

/* ── parallel passes ─────────────────────────────────────────────────────── */

typedef struct RenderJob {
  const FieldMotion *c;
  uint8_t *dst;
  size_t pitch;
  int scale, phases, frame_h;
} RenderJob;

/* Render phase `k` of `job` and weave it into the destination. Phases write
 * disjoint pixels, so workers never touch the same destination word. */
static void job_phase(Raster *r, const RenderJob *job, int k) {
  const int phases = job->phases, scale = job->scale;
  const int px = k % phases, py = k / phases, block = scale / phases;
  const double fx = (px + 0.5) / phases, fy = (py + 0.5) / phases;
  render_pass(r, job->c, fx, fy, k == 0 ? scale : 1);
  for (int y = 0; y < job->frame_h; y++) {
    const uint32_t *src = (const uint32_t *)(r->pass + (size_t)y * 256 * 4);
    for (int by = 0; by < block; by++) {
      uint32_t *row = (uint32_t *)(job->dst +
                                   (size_t)(y * scale + py + by) * job->pitch);
      if (block == 1) {
        for (int x = 0; x < 256; x++)
          row[x * scale + px] = src[x];
      } else {
        for (int x = 0; x < 256; x++)
          for (int bx = 0; bx < block; bx++)
            row[x * scale + bx] = src[x];
      }
    }
  }
}

static struct {
  pthread_mutex_t mu;
  pthread_cond_t work, done;
  int workers;          /* pool threads, excluding the caller */
  unsigned generation;
  const RenderJob *job;
  int next, total, finished;
  bool started;
} s_pool = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,
            PTHREAD_COND_INITIALIZER};

/* Claim and render phases until the job runs dry. Called with mu held. */
static void pool_drain(Raster *r) {
  while (s_pool.job && s_pool.next < s_pool.total) {
    const RenderJob *job = s_pool.job;
    int k = s_pool.next++;
    pthread_mutex_unlock(&s_pool.mu);
    job_phase(r, job, k);
    pthread_mutex_lock(&s_pool.mu);
    if (++s_pool.finished == s_pool.total)
      pthread_cond_signal(&s_pool.done);
  }
}

static void *pool_main(void *arg) {
  Raster *r = (Raster *)arg;
  unsigned seen = 0;
  pthread_mutex_lock(&s_pool.mu);
  for (;;) {
    while (s_pool.generation == seen)
      pthread_cond_wait(&s_pool.work, &s_pool.mu);
    seen = s_pool.generation;
    pool_drain(r);
  }
  return NULL;
}

static int cpu_count(void) {
#if SNESRECOMP_SDL3
  return SDL_GetNumLogicalCPUCores();
#else
  return SDL_GetCPUCount();
#endif
}

static void pool_start(void) {
  if (s_pool.started)
    return;
  s_pool.started = true;
  int want = cpu_count() - 2; /* leave the guest/audio thread a core */
  if (want > kMaxWorkers - 1) want = kMaxWorkers - 1;
  for (int i = 0; i < want; i++) {
    Raster *r = &s_raster[1 + i];
    pthread_t t;
    if (!raster_ready(r) || pthread_create(&t, NULL, pool_main, r) != 0)
      break;
    pthread_detach(t);
    s_pool.workers++;
  }
  fprintf(stderr, "[contra3] presenter threads: %d\n", 1 + s_pool.workers);
}

static void run_job(const RenderJob *job) {
  const int total = job->phases * job->phases;
  if (total > 1)
    pool_start();
  if (total == 1 || s_pool.workers == 0) {
    for (int k = 0; k < total; k++)
      job_phase(&s_raster[0], job, k);
    return;
  }
  pthread_mutex_lock(&s_pool.mu);
  s_pool.job = job;
  s_pool.next = 0;
  s_pool.finished = 0;
  s_pool.total = total;
  s_pool.generation++;
  pthread_cond_broadcast(&s_pool.work);
  pool_drain(&s_raster[0]);
  while (s_pool.finished < s_pool.total)
    pthread_cond_wait(&s_pool.done, &s_pool.mu);
  s_pool.job = NULL;
  pthread_mutex_unlock(&s_pool.mu);
}

double contra_interp_presentation_hz(double display_refresh) {
  cfg_load();
  if (!s_cfg.interp)
    return 0;
  if (s_cfg.hz > 0)
    return s_cfg.hz;
  if (display_refresh <= 0)
    return 1; /* "yes, decouple": the real rate is asked again with a window */
  return display_refresh;
}

int contra_interp_render_scale(void) {
  cfg_load();
  return s_cfg.scale;
}

int contra_interp_draw_frame(uint8_t *dst, size_t pitch, const uint8_t *field,
                             int frame_w, int frame_h, double alpha) {
  (void)field;
  if (!enhanced() || frame_w != 256 || frame_h > 224 || !g_ppu)
    return 0;
  /* The host sizes the destination for the scale it actually granted; a
   * thumbnail asks for native size. */
  int scale = s_cfg.scale;
  while (scale > 1 && pitch < (size_t)256 * scale * 4)
    scale--;
  const bool interp = s_cfg.interp && alpha < 0.999;
  if (scale == 1 && !interp)
    return 0;

  const FieldMotion *c = &s_fields[s_cur];
  const FieldMotion *p = &s_fields[s_cur ^ 1];
  if (!c->valid)
    return 0;
  const bool have_prev = p->valid && p->number + 1 == c->number;
  const double a = interp && have_prev ? alpha : 1.0;
  if (scale == 1 && a >= 1)
    return 0;
  if (!raster_ready(&s_raster[0]))
    return 0;
  if (!s_hd) {
    s_hd = (uint32_t *)calloc((size_t)256 * kMaxScale * 224 * kMaxScale, 4);
    if (!s_hd)
      return 0;
  }

  for (int line = 1; line <= 224; line++)
    pose_line(&s_pose[line], have_prev ? &p->line[line] : &c->line[line],
              &c->line[line], a);
  int moved = pose_oam(have_prev ? p : c, c, a, a < 1);

  /* N x N phase passes, interleaved. Phase k samples at (k + 0.5) / N
   * inside the native pixel. On a whole field (a == 1) every position is an
   * integer, all phases agree, and one pass fills each N x N block. */
  RenderJob job = {c, dst, pitch, scale, a < 1 ? scale : 1, frame_h};
  run_job(&job);
  /* Mode 7 scanlines: the HD sampler's picture replaces the phase weave. */
  if (scale > 1) {
    for (int y = 0; y < frame_h; y++) {
      const LineMotion *cl = &c->line[y + 1];
      if ((cl->bgmode & 7) != 7 || (cl->inidisp & 0x80))
        continue;
      for (int sy = 0; sy < scale; sy++)
        memcpy(dst + (size_t)(y * scale + sy) * pitch,
               s_hd + (size_t)(y * scale + sy) * 256 * scale,
               (size_t)256 * scale * 4);
    }
  }

  /* CONTRA_INTERP_LOG=1: one line per enhanced present. */
  {
    static int log = -1;
    if (log < 0)
      log = getenv("CONTRA_INTERP_LOG") ? 1 : 0;
    if (log)
      fprintf(stderr, "[interp] field=%u a=%.3f scale=%d bg1=%.2f bg2=%.2f sprites=%d\n",
              c->number, a, scale, s_pose[160].h[0], s_pose[160].h[1], moved);
  }
  return 1;
}
