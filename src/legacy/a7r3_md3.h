#ifndef A7R3_MD3_H
#define A7R3_MD3_H

#include <stdint.h>

/* Material Design 3 Dark theme tokens encoded in A7R3 RGBA4444. */
struct a7r3_md3_theme {
    uint16_t surface_high;
    uint16_t surface_card;
    uint16_t surface_focus;
    uint16_t primary;
    uint16_t on_primary;
    uint16_t primary_container;
    uint16_t on_primary_container;
    uint16_t outline;
    uint16_t outline_var;
    uint16_t on_surface;
    uint16_t on_surface_var;
};

/* Built-in Material You palettes: 0=Indigo, 1=Amber, 2=Teal. */
const struct a7r3_md3_theme *a7r3_md3_get_theme(int index);

/* Blend 4-bit alpha (0..15) fg color over bg RGBA4444 pixel. */
uint16_t a7r3_md3_blend_px(uint16_t bg, uint16_t fg, unsigned alpha4);

/* Cool dark translucent scrim over native background. */
void a7r3_md3_scrim(uint16_t *canvas);

/* 4x4 supersampled anti-aliased rounded rectangle fill & border stroke. */
void a7r3_md3_fill_rrect(uint16_t *canvas, int x, int y, int w, int h, int r, uint16_t color);
void a7r3_md3_fill_rrect_alpha(uint16_t *canvas, int x, int y, int w, int h, int r,
                               uint16_t color, unsigned alpha4);
void a7r3_md3_stroke_rrect(uint16_t *canvas, int x, int y, int w, int h, int r,
                           int thickness, uint16_t color);

/* Soft multi-pass MD3 elevation shadow under a rounded card. */
void a7r3_md3_shadow_rrect(uint16_t *canvas, int x, int y, int w, int h, int r);

/* 4x4 supersampled AA filled circle centered at (cx, cy). */
void a7r3_md3_fill_circle(uint16_t *canvas, int cx, int cy, int r, uint16_t color);

/* 4x4 supersampled AA rounded capsule / thick line segment from (x0,y0) to (x1,y1). */
void a7r3_md3_draw_line_aa(uint16_t *canvas, int x0, int y0, int x1, int y1,
                            int half_thick_q2, uint16_t color);

/* Authentic 52x32 MD3 Switch widget (ON/OFF with track, thumb & checkmark). */
void a7r3_md3_switch(uint16_t *canvas, int x, int y, int on,
                     const struct a7r3_md3_theme *th);

#endif /* A7R3_MD3_H */
