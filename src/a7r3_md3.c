#include "a7r3_md3.h"
#include "a7r3_draw.h"
#include "a7r3_fb.h"

static const struct a7r3_md3_theme g_themes[3] = {
    /* 0: Indigo Material You Dark */
    {
        .surface_high         = A7R3_RGBA4444(2, 2, 3, 15),
        .surface_card         = A7R3_RGBA4444(3, 3, 4, 15),
        .surface_focus        = A7R3_RGBA4444(4, 5, 7, 15),
        .primary              = A7R3_RGBA4444(11, 12, 15, 15),
        .on_primary           = A7R3_RGBA4444(2, 2, 5, 15),
        .primary_container    = A7R3_RGBA4444(4, 5, 8, 15),
        .on_primary_container = A7R3_RGBA4444(13, 14, 15, 15),
        .outline              = A7R3_RGBA4444(7, 7, 9, 15),
        .outline_var          = A7R3_RGBA4444(4, 4, 6, 15),
        .on_surface           = A7R3_RGBA4444(14, 14, 15, 15),
        .on_surface_var       = A7R3_RGBA4444(9, 10, 11, 15),
    },
    /* 1: Amber Material You Dark */
    {
        .surface_high         = A7R3_RGBA4444(3, 2, 2, 15),
        .surface_card         = A7R3_RGBA4444(4, 3, 3, 15),
        .surface_focus        = A7R3_RGBA4444(6, 4, 2, 15),
        .primary              = A7R3_RGBA4444(15, 11, 6, 15),
        .on_primary           = A7R3_RGBA4444(4, 2, 0, 15),
        .primary_container    = A7R3_RGBA4444(7, 4, 1, 15),
        .on_primary_container = A7R3_RGBA4444(15, 13, 10, 15),
        .outline              = A7R3_RGBA4444(9, 7, 6, 15),
        .outline_var          = A7R3_RGBA4444(5, 4, 3, 15),
        .on_surface           = A7R3_RGBA4444(15, 14, 13, 15),
        .on_surface_var       = A7R3_RGBA4444(11, 9, 8, 15),
    },
    /* 2: Teal Material You Dark */
    {
        .surface_high         = A7R3_RGBA4444(1, 3, 3, 15),
        .surface_card         = A7R3_RGBA4444(2, 4, 4, 15),
        .surface_focus        = A7R3_RGBA4444(2, 6, 6, 15),
        .primary              = A7R3_RGBA4444(7, 14, 13, 15),
        .on_primary           = A7R3_RGBA4444(0, 3, 3, 15),
        .primary_container    = A7R3_RGBA4444(1, 6, 6, 15),
        .on_primary_container = A7R3_RGBA4444(12, 15, 15, 15),
        .outline              = A7R3_RGBA4444(6, 9, 9, 15),
        .outline_var          = A7R3_RGBA4444(3, 5, 5, 15),
        .on_surface           = A7R3_RGBA4444(14, 15, 15, 15),
        .on_surface_var       = A7R3_RGBA4444(8, 11, 11, 15),
    },
};

const struct a7r3_md3_theme *a7r3_md3_get_theme(int index)
{
    if (index < 0 || index >= 3)
        index = 0;
    return &g_themes[index];
}

uint16_t a7r3_md3_blend_px(uint16_t bg, uint16_t fg, unsigned alpha4)
{
    unsigned inv, r, g, b;
    if (alpha4 == 0u)
        return bg;
    if (alpha4 >= 15u)
        return fg | 0x000fu;
    inv = 15u - alpha4;
    r = (((fg >> 12) & 0xfu) * alpha4 + ((bg >> 12) & 0xfu) * inv + 7u) / 15u;
    g = (((fg >> 8)  & 0xfu) * alpha4 + ((bg >> 8)  & 0xfu) * inv + 7u) / 15u;
    b = (((fg >> 4)  & 0xfu) * alpha4 + ((bg >> 4)  & 0xfu) * inv + 7u) / 15u;
    return A7R3_RGBA4444(r, g, b, 15u);
}

void a7r3_md3_scrim(uint16_t *canvas)
{
    unsigned i;
    if (!canvas)
        return;
    for (i = 0; i < A7R3_FB_PIXELS; i++) {
        uint16_t p = canvas[i];
        unsigned r = (((p >> 12) & 0xfu) * 5u) >> 4;
        unsigned g = (((p >> 8)  & 0xfu) * 5u) >> 4;
        unsigned b = (((p >> 4)  & 0xfu) * 6u) >> 4;
        canvas[i] = A7R3_RGBA4444(r, g, b, 15u);
    }
}

static int clamp_r(int w, int h, int r)
{
    int mr = (w < h ? w : h) / 2;
    if (r < 0)
        return 0;
    return (r > mr) ? mr : r;
}

static unsigned subpixel_hits_to_alpha4(unsigned hits, unsigned alpha4)
{
    unsigned cov = (hits >= 16u) ? 15u : ((hits * 15u + 8u) >> 4);
    if (alpha4 >= 15u)
        return cov;
    return (cov * alpha4 + 7u) / 15u;
}

void a7r3_md3_fill_rrect_alpha(uint16_t *canvas, int x, int y, int w, int h, int r,
                               uint16_t color, unsigned alpha4)
{
    int x0, y0, x1, y1, py, px;
    unsigned r8, r8sq;

    if (!canvas || w <= 0 || h <= 0 || alpha4 == 0u)
        return;
    r = clamp_r(w, h, r);
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = (x + w > (int)A7R3_FB_W) ? (int)A7R3_FB_W : (x + w);
    y1 = (y + h > (int)A7R3_FB_H) ? (int)A7R3_FB_H : (y + h);
    if (x0 >= x1 || y0 >= y1)
        return;

    r8 = (unsigned)r * 8u;
    r8sq = r8 * r8;

    for (py = y0; py < y1; py++) {
        int ly = py - y;
        int iy = (ly < r) ? (r - 1 - ly) : (ly >= h - r ? ly - (h - r) : -1);
        uint16_t *row = canvas + (unsigned)py * A7R3_FB_W;

        if (iy < 0) {
            for (px = x0; px < x1; px++) {
                row[px] = (alpha4 >= 15u)
                    ? (color | 0x000fu)
                    : a7r3_md3_blend_px(row[px], color, alpha4);
            }
            continue;
        }

        for (px = x0; px < x1; px++) {
            int lx = px - x;
            int ix = (lx < r) ? (r - 1 - lx) : (lx >= w - r ? lx - (w - r) : -1);
            if (ix < 0) {
                row[px] = (alpha4 >= 15u)
                    ? (color | 0x000fu)
                    : a7r3_md3_blend_px(row[px], color, alpha4);
            } else {
                unsigned sy, sx, hits = 0;
                for (sy = 0; sy < 4u; sy++) {
                    unsigned dy8 = (unsigned)iy * 8u + sy * 2u + 1u;
                    unsigned dy8sq = dy8 * dy8;
                    for (sx = 0; sx < 4u; sx++) {
                        unsigned dx8 = (unsigned)ix * 8u + sx * 2u + 1u;
                        if (dx8 * dx8 + dy8sq <= r8sq)
                            hits++;
                    }
                }
                if (hits) {
                    unsigned a = subpixel_hits_to_alpha4(hits, alpha4);
                    row[px] = a7r3_md3_blend_px(row[px], color, a);
                }
            }
        }
    }
}

void a7r3_md3_fill_rrect(uint16_t *canvas, int x, int y, int w, int h, int r, uint16_t color)
{
    a7r3_md3_fill_rrect_alpha(canvas, x, y, w, h, r, color, 15u);
}

void a7r3_md3_stroke_rrect(uint16_t *canvas, int x, int y, int w, int h, int r,
                           int thickness, uint16_t color)
{
    int x0, y0, x1, y1, py, px, inner_r;
    unsigned r8, r8sq, ir8, ir8sq;

    if (!canvas || w <= 0 || h <= 0 || thickness <= 0)
        return;
    r = clamp_r(w, h, r);
    inner_r = (r > thickness) ? (r - thickness) : 0;

    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = (x + w > (int)A7R3_FB_W) ? (int)A7R3_FB_W : (x + w);
    y1 = (y + h > (int)A7R3_FB_H) ? (int)A7R3_FB_H : (y + h);
    if (x0 >= x1 || y0 >= y1)
        return;

    r8 = (unsigned)r * 8u;
    r8sq = r8 * r8;
    ir8 = (unsigned)inner_r * 8u;
    ir8sq = ir8 * ir8;

    for (py = y0; py < y1; py++) {
        int ly = py - y;
        int iy = (ly < r) ? (r - 1 - ly) : (ly >= h - r ? ly - (h - r) : -1);
        int edge_y = (ly < thickness) || (ly >= h - thickness);
        uint16_t *row = canvas + (unsigned)py * A7R3_FB_W;

        for (px = x0; px < x1; px++) {
            int lx = px - x;
            int ix = (lx < r) ? (r - 1 - lx) : (lx >= w - r ? lx - (w - r) : -1);
            int edge_x = (lx < thickness) || (lx >= w - thickness);

            if (ix < 0 || iy < 0) {
                if (edge_x || edge_y)
                    row[px] = color | 0x000fu;
            } else {
                unsigned sy, sx, hits = 0;
                for (sy = 0; sy < 4u; sy++) {
                    unsigned dy8 = (unsigned)iy * 8u + sy * 2u + 1u;
                    unsigned dy8sq = dy8 * dy8;
                    for (sx = 0; sx < 4u; sx++) {
                        unsigned dx8 = (unsigned)ix * 8u + sx * 2u + 1u;
                        unsigned d2 = dx8 * dx8 + dy8sq;
                        if (d2 <= r8sq && d2 >= ir8sq)
                            hits++;
                    }
                }
                if (hits) {
                    unsigned a = subpixel_hits_to_alpha4(hits, 15u);
                    row[px] = a7r3_md3_blend_px(row[px], color, a);
                }
            }
        }
    }
}

void a7r3_md3_shadow_rrect(uint16_t *canvas, int x, int y, int w, int h, int r)
{
    uint16_t black = A7R3_RGBA4444(0, 0, 0, 15);
    a7r3_md3_fill_rrect_alpha(canvas, x - 5, y - 1, w + 10, h + 10, r + 4, black, 2u);
    a7r3_md3_fill_rrect_alpha(canvas, x - 3, y + 1, w + 6,  h + 6,  r + 2, black, 4u);
    a7r3_md3_fill_rrect_alpha(canvas, x - 1, y + 2, w + 2,  h + 3,  r + 1, black, 6u);
}

void a7r3_md3_fill_circle(uint16_t *canvas, int cx, int cy, int r, uint16_t color)
{
    int x0, y0, x1, y1, py, px;
    unsigned r8, r8sq;

    if (!canvas || r <= 0)
        return;
    x0 = (cx - r) < 0 ? 0 : (cx - r);
    y0 = (cy - r) < 0 ? 0 : (cy - r);
    x1 = (cx + r) > (int)A7R3_FB_W ? (int)A7R3_FB_W : (cx + r);
    y1 = (cy + r) > (int)A7R3_FB_H ? (int)A7R3_FB_H : (cy + r);
    r8 = (unsigned)r * 8u;
    r8sq = r8 * r8;

    for (py = y0; py < y1; py++) {
        uint16_t *row = canvas + (unsigned)py * A7R3_FB_W;
        for (px = x0; px < x1; px++) {
            int bx = (px - cx) * 8;
            int by = (py - cy) * 8;
            unsigned sy, sx, hits = 0;
            for (sy = 0; sy < 4u; sy++) {
                int dy = by + (int)sy * 2 + 1;
                int dy2 = dy * dy;
                for (sx = 0; sx < 4u; sx++) {
                    int dx = bx + (int)sx * 2 + 1;
                    if ((unsigned)(dx * dx + dy2) <= r8sq)
                        hits++;
                }
            }
            if (hits) {
                unsigned a = subpixel_hits_to_alpha4(hits, 15u);
                row[px] = a7r3_md3_blend_px(row[px], color, a);
            }
        }
    }
}

void a7r3_md3_draw_line_aa(uint16_t *canvas, int x0, int y0, int x1, int y1,
                            int half_thick_q2, uint16_t color)
{
    int minx, miny, maxx, maxy, pad, py, px;
    int vx, vy, len2, r8, r8sq;

    if (!canvas || half_thick_q2 <= 0)
        return;
    pad = (half_thick_q2 + 3) / 4 + 2;
    minx = (x0 < x1 ? x0 : x1) - pad;
    miny = (y0 < y1 ? y0 : y1) - pad;
    maxx = (x0 > x1 ? x0 : x1) + pad;
    maxy = (y0 > y1 ? y0 : y1) + pad;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > (int)A7R3_FB_W) maxx = (int)A7R3_FB_W;
    if (maxy > (int)A7R3_FB_H) maxy = (int)A7R3_FB_H;

    /* Scale centers to pixel-centers (*8 + 4) */
    vx = (x1 - x0) * 8;
    vy = (y1 - y0) * 8;
    len2 = vx * vx + vy * vy;
    r8 = half_thick_q2 * 2;
    r8sq = r8 * r8;

    for (py = miny; py < maxy; py++) {
        uint16_t *row = canvas + (unsigned)py * A7R3_FB_W;
        for (px = minx; px < maxx; px++) {
            unsigned sy, sx, hits = 0;
            for (sy = 0; sy < 4u; sy++) {
                int ppy = py * 8 + (int)sy * 2 + 1 - (y0 * 8 + 4);
                for (sx = 0; sx < 4u; sx++) {
                    int ppx = px * 8 + (int)sx * 2 + 1 - (x0 * 8 + 4);
                    int dx, dy;
                    if (len2 == 0) {
                        dx = ppx;
                        dy = ppy;
                    } else {
                        int dot = ppx * vx + ppy * vy;
                        if (dot <= 0) {
                            dx = ppx;
                            dy = ppy;
                        } else if (dot >= len2) {
                            dx = ppx - vx;
                            dy = ppy - vy;
                        } else {
                            /* Distance^2 <= r8^2 <=> (cross)^2 <= r8^2 * len2 */
                            long long cross = (long long)ppx * vy - (long long)ppy * vx;
                            if (cross * cross <= (long long)r8sq * (long long)len2)
                                hits++;
                            continue;
                        }
                    }
                    if (dx * dx + dy * dy <= r8sq)
                        hits++;
                }
            }
            if (hits) {
                unsigned a = subpixel_hits_to_alpha4(hits, 15u);
                row[px] = a7r3_md3_blend_px(row[px], color, a);
            }
        }
    }
}

void a7r3_md3_switch(uint16_t *canvas, int x, int y, int on,
                     const struct a7r3_md3_theme *th)
{
    if (!canvas || !th)
        return;
    if (on) {
        int tx = x + 36;
        int ty = y + 16;
        a7r3_md3_fill_rrect(canvas, x, y, 52, 32, 16, th->primary);
        a7r3_md3_fill_circle(canvas, tx, ty, 12, th->on_primary);
        a7r3_md3_draw_line_aa(canvas, tx - 5, ty, tx - 1, ty + 4, 4, th->primary);
        a7r3_md3_draw_line_aa(canvas, tx - 1, ty + 4, tx + 5, ty - 4, 4, th->primary);
    } else {
        a7r3_md3_fill_rrect(canvas, x, y, 52, 32, 16, th->surface_card);
        a7r3_md3_stroke_rrect(canvas, x, y, 52, 32, 16, 2, th->outline);
        a7r3_md3_fill_circle(canvas, x + 16, y + 16, 8, th->outline);
    }
}
