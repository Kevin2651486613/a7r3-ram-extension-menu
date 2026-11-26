#ifndef A7R3_DRAW_H
#define A7R3_DRAW_H

#include <stdint.h>

/* Sony A7R3 framebuffer native pixel layout: RGBA4444 in uint16_t
 * Bit 15..12 = R (0..15), 11..8 = G (0..15), 7..4 = B (0..15), 3..0 = A (0..15).
 * Note: alpha = 0xF is fully opaque on the A7R3 display pipeline. */
#define A7R3_RGBA4444(r4, g4, b4, a4) \
    ((uint16_t)((((unsigned)(r4) & 0xfu) << 12) | \
                (((unsigned)(g4) & 0xfu) << 8)  | \
                (((unsigned)(b4) & 0xfu) << 4)  | \
                ((unsigned)(a4) & 0xfu)))

#define A7R3_RGB888(r8, g8, b8) \
    A7R3_RGBA4444(((unsigned)(r8) >> 4), ((unsigned)(g8) >> 4), ((unsigned)(b8) >> 4), 0xfu)

void a7r3_draw_clear(uint16_t *canvas, uint16_t color);
void a7r3_draw_fill_rect(uint16_t *canvas, int x, int y, int w, int h, uint16_t color);
void a7r3_draw_stroke_rect(uint16_t *canvas, int x, int y, int w, int h, int thickness, uint16_t color);
void a7r3_draw_dim_rect(uint16_t *canvas, int x, int y, int w, int h);
void a7r3_draw_blit_sprite(uint16_t *canvas, int dx, int dy, int w, int h, const uint16_t *rgba4444);

/* Blend 4-bit alpha mask (0..15 per byte) over canvas pixels using fg color. */
void a7r3_draw_alpha4(uint16_t *canvas, int dx, int dy, int w, int h,
                      const uint8_t *alpha4, uint16_t fg);

/* Built-in scaled 5x7 ASCII font (scale >= 1) for immediate add-on UI labels. */
void a7r3_draw_text_ascii(uint16_t *canvas, int x, int y, const char *s,
                          uint16_t color, int scale);

#endif /* A7R3_DRAW_H */
