#include "a7r3_sys.h"
#include "a7r3_fb.h"
#include "a7r3_draw.h"
#include "a7r3_md3.h"
#include "a7r3_input.h"
#include "a7r3_dual_hud.h"
#include "a7r3_pb_focus.h"
#include "a7r3_entry_cert.h"
#include "a7r3_shutter_sfx.h"
#include "md3_demo_labels.h"

#define GATE_ARM  "/tmp_bt/addon_arm"
#define GATE_OFF  "/tmp_bt/addon_off"
#define GATE_OPEN "/tmp_bt/addon_open"
#define GATE_QUIT "/tmp_bt/addon_quit"

#define CARD_X    56
#define CARD_Y    30
#define CARD_W    528
#define CARD_H    420
#define CARD_R    28

#define ROW_X     76
#define ROW_Y0    110
#define ROW_W     488
#define ROW_H     58
#define ROW_GAP   8
#define ROW_STEP  (ROW_H + ROW_GAP)
#define ROW_R     18
#define ITEM_CNT  4
#define SFX_CNT   4
#define SFX_SLOTS 6

#define PAGE_CNT  2

#define SFX_BACK_X 76
#define SFX_BACK_Y 50
#define SFX_BACK_W 36
#define SFX_BACK_H 36
#define SFX_SEL_X  376
#define SFX_SEL_W  176
#define SFX_SEL_H  34
#define SFX_VOL_X0 388
#define SFX_VOL_W  50
#define SFX_VOL_GAP 8
#define SFX_VOL_H  32
#define SFX_ACT_PRV_X 468
#define SFX_ACT_PRV_W 84
#define SFX_ACT_H  32
#define BTN_BACK_X 76
#define BTN_BACK_W 116
#define BTN_RESCAN_X 200
#define BTN_RESCAN_W 104

#define DD_LBL_X    342
#define DD_X        382
#define DD_Y        51
#define DD_W        104
#define DD_H        30
#define DD_MENU_X   374
#define DD_MENU_Y   85
#define DD_MENU_W   112
#define DD_MENU_PAD 6
#define DD_ITEM_H   32
#define DD_MENU_H   (DD_MENU_PAD * 2 + 3 * DD_ITEM_H)

#define BTN_Y      392
#define BTN_H      36
#define BTN_PREV_X 76
#define BTN_PREV_W 42
#define PAGE_IND_X 124
#define PAGE_IND_W 62
#define BTN_NEXT_X 192
#define BTN_NEXT_W 42
#define BTN_RST_X  364
#define BTN_RST_W  96
#define BTN_DON_X  470
#define BTN_DON_W  90

extern int pthread_create(unsigned long *tid, const void *attr,
                          void *(*fn)(void *), void *arg)
    __attribute__((weak));

static uint16_t g_canvas[A7R3_FB_PIXELS];
static uint16_t g_native_bg[A7R3_FB_PIXELS];
static volatile int g_native_ok;

static volatile int g_open;
static volatile int g_dirty;
static volatile int g_drawing;
static unsigned g_open_ms;
static unsigned g_ref_deadlines[4];
static volatile int g_ref_idx = 4;
static int g_sel;
static int g_page;
static int g_theme;
static int g_dd_open;
static int g_dd_sel;
static int g_submenu;
static int g_sfx_sel;
static int g_sfx_scanned_once;
static int g_last_eshut = -1;
static int g_last_sd_cnt = -1;
static int g_item_vals[3] = { 0, 0, 0 };

static const uint16_t g_theme_swatches[3] = {
    A7R3_RGBA4444(11, 12, 15, 15),
    A7R3_RGBA4444(15, 11, 6, 15),
    A7R3_RGBA4444(7, 14, 13, 15),
};

static const struct md3_label *const g_row_titles[ITEM_CNT] = {
    &LBL_ROW0_T, &LBL_ROW1_T, &LBL_ROW2_T, &LBL_ROW3_T
};
static const struct md3_label *const g_row_subs[ITEM_CNT] = {
    &LBL_ROW0_S, &LBL_ROW1_S, &LBL_ROW2_S, &LBL_ROW3_S
};
static const struct md3_label *const g_sfx_titles[SFX_CNT] = {
    &LBL_SFX_R0_T, &LBL_SFX_R1_T, &LBL_SFX_R2_T, &LBL_SFX_R3_T
};
static const struct md3_label *const g_sfx_subs[SFX_CNT] = {
    &LBL_SFX_R0_S, &LBL_SFX_R1_S, &LBL_SFX_R2_S, &LBL_SFX_R3_S
};
static const struct md3_label *const g_sfx_presets[A7R3_SFX_BUILTIN_CNT] = {
    &LBL_SFX_P0, &LBL_SFX_P1, &LBL_SFX_P2
};
static const struct md3_label *const g_vol_labels[3] = {
    &LBL_VOL_0, &LBL_VOL_1, &LBL_VOL_2
};
static const struct md3_label *const g_chip_labels[3] = {
    &LBL_CHIP0, &LBL_CHIP1, &LBL_CHIP2
};
static const struct md3_label *const g_page_labels[PAGE_CNT] = {
    &LBL_PAGE_1_2, &LBL_PAGE_2_2
};

static int page_row_count(int page)
{
    return (page == 0) ? ITEM_CNT : 0;
}

static void draw_lbl(int x, int y, const struct md3_label *lbl, uint16_t color)
{
    if (!lbl || !lbl->alpha)
        return;
    a7r3_draw_alpha4(g_canvas, x, y, (int)lbl->w, (int)lbl->h, lbl->alpha, color);
}

static void draw_lbl_centered(int bx, int by, int bw, int bh,
                              const struct md3_label *lbl, uint16_t color)
{
    int lx, ly;
    if (!lbl || !lbl->alpha)
        return;
    lx = bx + (bw - (int)lbl->w) / 2;
    ly = by + (bh - (int)lbl->h) / 2;
    draw_lbl(lx, ly, lbl, color);
}

static void draw_header_icon(int cx, int cy, const struct a7r3_md3_theme *th)
{
    a7r3_md3_fill_circle(g_canvas, cx, cy, 20, th->primary_container);
    a7r3_md3_stroke_rrect(g_canvas, cx - 10, cy - 10, 20, 20, 10, 2, th->on_primary_container);
    a7r3_md3_fill_circle(g_canvas, cx, cy, 4, th->primary);
}

static void draw_row_icon(int ix, int iy, int row, const struct a7r3_md3_theme *th)
{
    uint16_t fg = th->on_primary_container;
    a7r3_md3_fill_rrect(g_canvas, ix, iy, 36, 36, 12, th->primary_container);

    switch (row) {
    case 0: /* EV dial fixed to 0 / shielded */
        a7r3_md3_stroke_rrect(g_canvas, ix + 8, iy + 8, 20, 20, 10, 2, fg);
        a7r3_md3_stroke_rrect(g_canvas, ix + 15, iy + 13, 6, 10, 3, 2, th->primary);
        break;
    case 1: /* Dual HUD: Level line + live Histogram bars */
        a7r3_md3_draw_line_aa(g_canvas, ix + 8, iy + 13, ix + 28, iy + 13, 2, th->primary);
        a7r3_draw_fill_rect(g_canvas, ix + 9,  iy + 21, 3, 6, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 14, iy + 18, 3, 9, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 19, iy + 20, 3, 7, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 24, iy + 23, 3, 4, fg);
        break;
    case 2: /* Playback AF focus reticle: 4 corners + center dot */
        a7r3_draw_fill_rect(g_canvas, ix + 9,  iy + 9,  6, 2, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 9,  iy + 9,  2, 6, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 21, iy + 9,  6, 2, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 25, iy + 9,  2, 6, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 9,  iy + 25, 6, 2, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 9,  iy + 21, 2, 6, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 21, iy + 25, 6, 2, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 25, iy + 21, 2, 6, fg);
        a7r3_md3_fill_circle(g_canvas, ix + 18, iy + 18, 2, th->primary);
        break;
    case 3: /* Speaker + electronic shutter soundwave icon */
    default:
        a7r3_draw_fill_rect(g_canvas, ix + 8, iy + 14, 5, 8, fg);
        a7r3_md3_draw_line_aa(g_canvas, ix + 12, iy + 14, ix + 18, iy + 10, 3, fg);
        a7r3_md3_draw_line_aa(g_canvas, ix + 18, iy + 10, ix + 18, iy + 26, 3, fg);
        a7r3_md3_draw_line_aa(g_canvas, ix + 12, iy + 22, ix + 18, iy + 26, 3, fg);
        a7r3_md3_draw_line_aa(g_canvas, ix + 22, iy + 13, ix + 24, iy + 18, 2, th->primary);
        a7r3_md3_draw_line_aa(g_canvas, ix + 24, iy + 18, ix + 22, iy + 23, 2, th->primary);
        a7r3_md3_draw_line_aa(g_canvas, ix + 26, iy + 10, ix + 28, iy + 18, 2, th->primary);
        a7r3_md3_draw_line_aa(g_canvas, ix + 28, iy + 18, ix + 26, iy + 26, 2, th->primary);
        break;
    }
}

static void draw_sfx_row_icon(int ix, int iy, int row, const struct a7r3_md3_theme *th)
{
    uint16_t fg = th->on_primary_container;
    a7r3_md3_fill_rrect(g_canvas, ix, iy, 36, 36, 12, th->primary_container);

    switch (row) {
    case 0: /* Power / e-shutter sound switch */
        a7r3_md3_stroke_rrect(g_canvas, ix + 9, iy + 9, 18, 18, 9, 2, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 17, iy + 7, 2, 10, th->primary);
        break;
    case 1: /* Music note / SD audio file */
        a7r3_draw_fill_rect(g_canvas, ix + 13, iy + 10, 12, 3, th->primary);
        a7r3_draw_fill_rect(g_canvas, ix + 13, iy + 12, 2, 12, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 23, iy + 12, 2, 10, fg);
        a7r3_md3_fill_circle(g_canvas, ix + 11, iy + 24, 3, fg);
        a7r3_md3_fill_circle(g_canvas, ix + 21, iy + 22, 3, fg);
        break;
    case 2: /* Equalizer / volume bars */
        a7r3_draw_fill_rect(g_canvas, ix + 10, iy + 20, 4, 7, fg);
        a7r3_draw_fill_rect(g_canvas, ix + 16, iy + 15, 4, 12, th->primary);
        a7r3_draw_fill_rect(g_canvas, ix + 22, iy + 10, 4, 17, fg);
        break;
    case 3: /* Play triangle */
    default:
        a7r3_md3_draw_line_aa(g_canvas, ix + 13, iy + 10, ix + 25, iy + 18, 3, th->primary);
        a7r3_md3_draw_line_aa(g_canvas, ix + 25, iy + 18, ix + 13, iy + 26, 3, th->primary);
        a7r3_draw_fill_rect(g_canvas, ix + 12, iy + 10, 3, 16, fg);
        break;
    }
}

static void draw_page_chevron(int cx, int cy, int dir, uint16_t color)
{
    int tip_x = (dir < 0) ? (cx - 4) : (cx + 4);
    int arm_x = (dir < 0) ? (cx + 3) : (cx - 3);
    a7r3_md3_draw_line_aa(g_canvas, arm_x, cy - 6, tip_x, cy, 4, color);
    a7r3_md3_draw_line_aa(g_canvas, tip_x, cy, arm_x, cy + 6, 4, color);
}

static void draw_dd_chevron(int cx, int cy, int up, uint16_t color)
{
    int tip_y = up ? (cy - 2) : (cy + 2);
    int arm_y = up ? (cy + 2) : (cy - 2);
    a7r3_md3_draw_line_aa(g_canvas, cx - 4, arm_y, cx, tip_y, 3, color);
    a7r3_md3_draw_line_aa(g_canvas, cx, tip_y, cx + 4, arm_y, 3, color);
}

static void enter_sfx_submenu(void)
{
    g_submenu = 1;
    g_sfx_sel = 0;
    g_dd_open = 0;
    a7r3_shutter_sfx_refresh_state();
    if (!g_sfx_scanned_once) {
        a7r3_shutter_sfx_rescan_sd();
        g_sfx_scanned_once = 1;
    }
    g_dirty = 1;
}

static void render_sfx_submenu(const struct a7r3_md3_theme *th)
{
    int r;
    int eshut = a7r3_shutter_sfx_is_eshutter();
    int sfx_on = a7r3_shutter_sfx_get_enabled();
    int snd_idx = a7r3_shutter_sfx_get_selected();
    int vol = a7r3_shutter_sfx_get_volume();
    int rescan_focus = (g_sfx_sel == 4);
    int back_focus = (g_sfx_sel == 5);

    /* Header: Back button + Title + Live Electronic Shutter badge */
    a7r3_md3_fill_rrect(g_canvas, SFX_BACK_X, SFX_BACK_Y, SFX_BACK_W, SFX_BACK_H, 12,
                        back_focus ? th->primary_container : th->surface_card);
    a7r3_md3_stroke_rrect(g_canvas, SFX_BACK_X, SFX_BACK_Y, SFX_BACK_W, SFX_BACK_H, 12,
                          back_focus ? 2 : 1, back_focus ? th->primary : th->outline);
    draw_page_chevron(SFX_BACK_X + SFX_BACK_W / 2, SFX_BACK_Y + SFX_BACK_H / 2, -1,
                      back_focus ? th->on_primary_container : th->on_surface);

    draw_lbl(CARD_X + 68, CARD_Y + 21, &LBL_SFX_TITLE, th->on_surface);
    draw_lbl(CARD_X + 68, CARD_Y + 48, &LBL_SFX_SUB, th->primary);

    a7r3_md3_fill_rrect(g_canvas, CARD_X + CARD_W - 114, CARD_Y + 24, 92, 26, 13,
                        eshut ? th->primary_container : th->surface_card);
    a7r3_md3_stroke_rrect(g_canvas, CARD_X + CARD_W - 114, CARD_Y + 24, 92, 26, 13, 1,
                          eshut ? th->primary : th->outline_var);
    draw_lbl_centered(CARD_X + CARD_W - 114, CARD_Y + 24, 92, 26,
                      eshut ? &LBL_ESHUT_ON : &LBL_ESHUT_OFF,
                      eshut ? th->on_primary_container : th->on_surface_var);

    for (r = 0; r < SFX_CNT; r++) {
        int ry = ROW_Y0 + r * ROW_STEP;
        int sel = (r == g_sfx_sel);
        uint16_t row_bg = sel ? th->surface_focus : th->surface_card;

        a7r3_md3_fill_rrect(g_canvas, ROW_X, ry, ROW_W, ROW_H, ROW_R, row_bg);
        a7r3_md3_stroke_rrect(g_canvas, ROW_X, ry, ROW_W, ROW_H, ROW_R,
                              sel ? 2 : 1, sel ? th->primary : th->outline_var);

        draw_sfx_row_icon(ROW_X + 12, ry + 11, r, th);
        draw_lbl(ROW_X + 58, ry + 11, g_sfx_titles[r], th->on_surface);
        draw_lbl(ROW_X + 58, ry + 33, g_sfx_subs[r], th->on_surface_var);

        if (r == 0) {
            a7r3_md3_switch(g_canvas, ROW_X + ROW_W - 66, ry + 13, sfx_on, th);
        } else if (r == 1) {
            int sy = ry + (ROW_H - SFX_SEL_H) / 2;
            const char *sd_name = a7r3_shutter_sfx_name(snd_idx);
            a7r3_md3_fill_rrect(g_canvas, SFX_SEL_X, sy, SFX_SEL_W, SFX_SEL_H, 12,
                                th->surface_high);
            a7r3_md3_stroke_rrect(g_canvas, SFX_SEL_X, sy, SFX_SEL_W, SFX_SEL_H, 12, 1,
                                  sel ? th->primary : th->outline);
            draw_page_chevron(SFX_SEL_X + 12, sy + SFX_SEL_H / 2, -1, th->on_surface_var);
            draw_page_chevron(SFX_SEL_X + SFX_SEL_W - 12, sy + SFX_SEL_H / 2, 1, th->on_surface_var);
            if (!sd_name && snd_idx >= 0 && snd_idx < A7R3_SFX_BUILTIN_CNT) {
                draw_lbl_centered(SFX_SEL_X + 22, sy, SFX_SEL_W - 44, SFX_SEL_H,
                                  g_sfx_presets[snd_idx], th->on_surface);
            } else if (sd_name) {
                char short_nm[14];
                int n = 0;
                int tx;
                while (n < 11 && sd_name[n]) {
                    short_nm[n] = sd_name[n];
                    n++;
                }
                short_nm[n] = '\0';
                tx = SFX_SEL_X + 24 + ((SFX_SEL_W - 48) - n * 6) / 2;
                if (tx < SFX_SEL_X + 24)
                    tx = SFX_SEL_X + 24;
                a7r3_draw_text_ascii(g_canvas, tx, sy + (SFX_SEL_H - 7) / 2,
                                     short_nm, th->primary, 1);
            }
        } else if (r == 2) {
            int c;
            int cy = ry + (ROW_H - SFX_VOL_H) / 2;
            for (c = 0; c < 3; c++) {
                int cx = SFX_VOL_X0 + c * (SFX_VOL_W + SFX_VOL_GAP);
                int active = (c == vol);
                a7r3_md3_fill_rrect(g_canvas, cx, cy, SFX_VOL_W, SFX_VOL_H, 12,
                                    active ? th->primary_container : th->surface_high);
                a7r3_md3_stroke_rrect(g_canvas, cx, cy, SFX_VOL_W, SFX_VOL_H, 12, 1,
                                      active ? th->primary : th->outline_var);
                draw_lbl_centered(cx, cy, SFX_VOL_W, SFX_VOL_H, g_vol_labels[c],
                                  active ? th->on_primary_container : th->on_surface_var);
            }
        } else {
            int ay = ry + (ROW_H - SFX_ACT_H) / 2;
            a7r3_md3_fill_rrect(g_canvas, SFX_ACT_PRV_X, ay, SFX_ACT_PRV_W, SFX_ACT_H, 12,
                                sel ? th->primary_container : th->surface_high);
            a7r3_md3_stroke_rrect(g_canvas, SFX_ACT_PRV_X, ay, SFX_ACT_PRV_W, SFX_ACT_H, 12,
                                  sel ? 2 : 1, sel ? th->primary : th->outline);
            draw_lbl_centered(SFX_ACT_PRV_X, ay, SFX_ACT_PRV_W, SFX_ACT_H,
                              &LBL_BTN_PREVIEW,
                              sel ? th->on_primary_container : th->on_surface);
        }
    }

    /* Footer: Back to main MD3 menu + Rescan SD + Done */
    a7r3_md3_fill_rrect(g_canvas, BTN_BACK_X, BTN_Y, BTN_BACK_W, BTN_H, 18,
                        back_focus ? th->primary_container : th->surface_card);
    a7r3_md3_stroke_rrect(g_canvas, BTN_BACK_X, BTN_Y, BTN_BACK_W, BTN_H, 18,
                          back_focus ? 2 : 1, back_focus ? th->primary : th->outline);
    draw_page_chevron(BTN_BACK_X + 16, BTN_Y + BTN_H / 2, -1,
                      back_focus ? th->on_primary_container : th->on_surface);
    draw_lbl_centered(BTN_BACK_X + 20, BTN_Y, BTN_BACK_W - 24, BTN_H, &LBL_BTN_BACK,
                      back_focus ? th->on_primary_container : th->on_surface);

    a7r3_md3_fill_rrect(g_canvas, BTN_RESCAN_X, BTN_Y, BTN_RESCAN_W, BTN_H, 18,
                        rescan_focus ? th->primary_container : th->surface_card);
    a7r3_md3_stroke_rrect(g_canvas, BTN_RESCAN_X, BTN_Y, BTN_RESCAN_W, BTN_H, 18,
                          rescan_focus ? 2 : 1, rescan_focus ? th->primary : th->outline);
    draw_lbl_centered(BTN_RESCAN_X, BTN_Y, BTN_RESCAN_W, BTN_H, &LBL_BTN_RESCAN,
                      rescan_focus ? th->on_primary_container : th->on_surface);

    a7r3_md3_fill_rrect(g_canvas, BTN_DON_X, BTN_Y, BTN_DON_W, BTN_H, 18, th->primary);
    draw_lbl_centered(BTN_DON_X, BTN_Y, BTN_DON_W, BTN_H, &LBL_BTN_DONE, th->on_primary);
}

static void render_addon_card(void)
{
    const struct a7r3_md3_theme *th = a7r3_md3_get_theme(g_theme);
    unsigned i;
    int r, rows, pag_focus, dd_focus;
    uint16_t pag_bg, pag_bd, pag_fg;

    if (g_native_ok) {
        for (i = 0; i < A7R3_FB_PIXELS; i++)
            g_canvas[i] = g_native_bg[i];
        a7r3_md3_scrim(g_canvas);
    } else {
        a7r3_draw_clear(g_canvas, A7R3_RGBA4444(1, 1, 2, 15));
    }

    rows = page_row_count(g_page);
    pag_focus = (g_sel == rows);
    dd_focus = (g_sel == rows + 1);

    /* Elevated Material You dialog surface */
    a7r3_md3_shadow_rrect(g_canvas, CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R);
    a7r3_md3_fill_rrect(g_canvas, CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R, th->surface_high);
    a7r3_md3_stroke_rrect(g_canvas, CARD_X, CARD_Y, CARD_W, CARD_H, CARD_R, 1, th->outline_var);

    if (g_submenu == 1) {
        render_sfx_submenu(th);
        a7r3_fb_pack_frame(g_canvas);
        a7r3_fb_present_full();
        g_dirty = 0;
        return;
    }

    /* Dialog header */
    draw_header_icon(CARD_X + 44, CARD_Y + 38, th);
    draw_lbl(CARD_X + 76, CARD_Y + 21, &LBL_TITLE, th->on_surface);
    draw_lbl(CARD_X + 76, CARD_Y + 47, &LBL_SUBTITLE, th->on_surface_var);

    /* Compact header theme dropdown */
    draw_lbl(DD_LBL_X, DD_Y + (DD_H - (int)LBL_THEME_PREFIX.h) / 2,
             &LBL_THEME_PREFIX, th->on_surface_var);
    a7r3_md3_fill_rrect(g_canvas, DD_X, DD_Y, DD_W, DD_H, 12,
                        (dd_focus || g_dd_open) ? th->surface_focus : th->surface_card);
    a7r3_md3_stroke_rrect(g_canvas, DD_X, DD_Y, DD_W, DD_H, 12,
                          (dd_focus || g_dd_open) ? 2 : 1,
                          (dd_focus || g_dd_open) ? th->primary : th->outline);
    a7r3_md3_fill_circle(g_canvas, DD_X + 15, DD_Y + DD_H / 2, 4, g_theme_swatches[g_theme]);
    draw_lbl_centered(DD_X + 22, DD_Y, DD_W - 40, DD_H, g_chip_labels[g_theme], th->on_surface);
    draw_dd_chevron(DD_X + DD_W - 13, DD_Y + DD_H / 2, g_dd_open,
                    (dd_focus || g_dd_open) ? th->primary : th->on_surface_var);

    a7r3_md3_fill_rrect(g_canvas, CARD_X + CARD_W - 86, CARD_Y + 24, 62, 24, 12, th->primary_container);
    draw_lbl_centered(CARD_X + CARD_W - 86, CARD_Y + 24, 62, 24, &LBL_BADGE_RAM, th->on_primary_container);

    /* Page content */
    if (g_page == 0) {
        for (r = 0; r < ITEM_CNT; r++) {
            int ry = ROW_Y0 + r * ROW_STEP;
            int sel = (r == g_sel);
            uint16_t row_bg = sel ? th->surface_focus : th->surface_card;

            a7r3_md3_fill_rrect(g_canvas, ROW_X, ry, ROW_W, ROW_H, ROW_R, row_bg);
            if (sel)
                a7r3_md3_stroke_rrect(g_canvas, ROW_X, ry, ROW_W, ROW_H, ROW_R, 2, th->primary);
            else
                a7r3_md3_stroke_rrect(g_canvas, ROW_X, ry, ROW_W, ROW_H, ROW_R, 1, th->outline_var);

            draw_row_icon(ROW_X + 12, ry + 11, r, th);
            draw_lbl(ROW_X + 60, ry + 11, g_row_titles[r], th->on_surface);
            draw_lbl(ROW_X + 60, ry + 33, g_row_subs[r], th->on_surface_var);
            if (r < 3) {
                a7r3_md3_switch(g_canvas, ROW_X + ROW_W - 66, ry + 13, g_item_vals[r], th);
            } else {
                int sfx_on = a7r3_shutter_sfx_get_enabled();
                a7r3_md3_fill_rrect(g_canvas, ROW_X + ROW_W - 96, ry + 16, 58, 26, 13,
                                    sfx_on ? th->primary_container : th->surface_high);
                a7r3_md3_stroke_rrect(g_canvas, ROW_X + ROW_W - 96, ry + 16, 58, 26, 13, 1,
                                      sfx_on ? th->primary : th->outline_var);
                draw_lbl_centered(ROW_X + ROW_W - 96, ry + 16, 58, 26,
                                  sfx_on ? &LBL_STATE_ON : &LBL_STATE_OFF,
                                  sfx_on ? th->on_primary_container : th->on_surface_var);
                draw_page_chevron(ROW_X + ROW_W - 22, ry + ROW_H / 2, 1,
                                  sel ? th->primary : th->on_surface_var);
            }
        }
    } else {
        int plus_cx = CARD_X + CARD_W / 2;
        int plus_cy = 210;
        a7r3_md3_fill_rrect(g_canvas, ROW_X, 172, ROW_W, 132, 22, th->surface_card);
        a7r3_md3_stroke_rrect(g_canvas, ROW_X, 172, ROW_W, 132, 22, 1, th->outline_var);
        a7r3_md3_fill_circle(g_canvas, plus_cx, plus_cy, 18, th->primary_container);
        a7r3_md3_draw_line_aa(g_canvas, plus_cx - 7, plus_cy, plus_cx + 7, plus_cy, 4, th->on_primary_container);
        a7r3_md3_draw_line_aa(g_canvas, plus_cx, plus_cy - 7, plus_cx, plus_cy + 7, 4, th->on_primary_container);
        draw_lbl_centered(ROW_X, 238, ROW_W, 24, &LBL_PAGE2_T, th->on_surface);
        draw_lbl_centered(ROW_X, 268, ROW_W, 20, &LBL_PAGE2_S, th->on_surface_var);
    }

    /* Bottom action bar: left pagination controls + right Reset/Done */
    pag_bg = pag_focus ? th->primary_container : th->surface_card;
    pag_bd = pag_focus ? th->primary : th->outline;
    pag_fg = pag_focus ? th->on_primary_container : th->on_surface;

    a7r3_md3_fill_rrect(g_canvas, BTN_PREV_X, BTN_Y, BTN_PREV_W, BTN_H, 18, pag_bg);
    a7r3_md3_stroke_rrect(g_canvas, BTN_PREV_X, BTN_Y, BTN_PREV_W, BTN_H, 18, pag_focus ? 2 : 1, pag_bd);
    draw_page_chevron(BTN_PREV_X + BTN_PREV_W / 2, BTN_Y + BTN_H / 2, -1, pag_fg);

    a7r3_md3_fill_rrect(g_canvas, PAGE_IND_X, BTN_Y, PAGE_IND_W, BTN_H, 18, th->surface_card);
    a7r3_md3_stroke_rrect(g_canvas, PAGE_IND_X, BTN_Y, PAGE_IND_W, BTN_H, 18,
                          pag_focus ? 2 : 1, pag_focus ? th->primary : th->outline_var);
    draw_lbl_centered(PAGE_IND_X, BTN_Y, PAGE_IND_W, BTN_H, g_page_labels[g_page], th->on_surface);

    a7r3_md3_fill_rrect(g_canvas, BTN_NEXT_X, BTN_Y, BTN_NEXT_W, BTN_H, 18, pag_bg);
    a7r3_md3_stroke_rrect(g_canvas, BTN_NEXT_X, BTN_Y, BTN_NEXT_W, BTN_H, 18, pag_focus ? 2 : 1, pag_bd);
    draw_page_chevron(BTN_NEXT_X + BTN_NEXT_W / 2, BTN_Y + BTN_H / 2, 1, pag_fg);

    a7r3_md3_fill_rrect(g_canvas, BTN_RST_X, BTN_Y, BTN_RST_W, BTN_H, 18, th->primary_container);
    draw_lbl_centered(BTN_RST_X, BTN_Y, BTN_RST_W, BTN_H, &LBL_BTN_RESET, th->on_primary_container);

    a7r3_md3_fill_rrect(g_canvas, BTN_DON_X, BTN_Y, BTN_DON_W, BTN_H, 18, th->primary);
    draw_lbl_centered(BTN_DON_X, BTN_Y, BTN_DON_W, BTN_H, &LBL_BTN_DONE, th->on_primary);

    /* Overlay elevated theme dropdown popup when open */
    if (g_dd_open) {
        int d;
        a7r3_md3_shadow_rrect(g_canvas, DD_MENU_X, DD_MENU_Y, DD_MENU_W, DD_MENU_H, 14);
        a7r3_md3_fill_rrect(g_canvas, DD_MENU_X, DD_MENU_Y, DD_MENU_W, DD_MENU_H, 14, th->surface_high);
        a7r3_md3_stroke_rrect(g_canvas, DD_MENU_X, DD_MENU_Y, DD_MENU_W, DD_MENU_H, 14, 1, th->outline);
        for (d = 0; d < 3; d++) {
            int iy = DD_MENU_Y + DD_MENU_PAD + d * DD_ITEM_H;
            int sel = (d == g_dd_sel);
            int mid_y = iy + DD_ITEM_H / 2;
            if (sel) {
                a7r3_md3_fill_rrect(g_canvas, DD_MENU_X + 6, iy + 2, DD_MENU_W - 12,
                                    DD_ITEM_H - 4, 10, th->primary_container);
            }
            a7r3_md3_fill_circle(g_canvas, DD_MENU_X + 20, mid_y, 5, g_theme_swatches[d]);
            draw_lbl(DD_MENU_X + 34, iy + (DD_ITEM_H - (int)g_chip_labels[d]->h) / 2,
                     g_chip_labels[d], sel ? th->on_primary_container : th->on_surface);
            if (d == g_theme) {
                int cx = DD_MENU_X + DD_MENU_W - 20;
                a7r3_md3_draw_line_aa(g_canvas, cx - 4, mid_y, cx - 1, mid_y + 3, 3, th->primary);
                a7r3_md3_draw_line_aa(g_canvas, cx - 1, mid_y + 3, cx + 4, mid_y - 3, 3, th->primary);
            }
        }
    }

    a7r3_fb_pack_frame(g_canvas);
    a7r3_fb_present_full();
    g_dirty = 0;
}

static void open_addon(void)
{
    if (g_open)
        return;
    a7r3_input_flush();
    a7r3_fb_pb_box_hide();
    g_native_ok = (a7r3_fb_save_native(g_native_bg) == 0);
    g_ref_idx = 4;
    g_open = 1;
    g_dirty = 1;
    g_open_ms = a7r3_now_ms();
    a7r3_input_set_swallow(1, 1);
    a7r3_input_flush();
    a7r3_fb_set_guard_active(1);
    a7r3_log_str("SDK_MD3_OPEN\n");
}

static void close_addon(void)
{
    if (!g_open)
        return;
    g_open = 0;
    g_dirty = 0;
    g_dd_open = 0;
    g_submenu = 0;
    g_ref_idx = 4;
    a7r3_fb_set_guard_active(0);
    a7r3_input_set_swallow(0, 0);
    if (g_native_ok) {
        a7r3_fb_restore_saved();
        g_native_ok = 0;
    }
    a7r3_log_str("SDK_MD3_CLOSE\n");
}

static void on_external_dismiss(unsigned code, unsigned status)
{
    int spin;
    (void)code;
    (void)status;
    a7r3_fb_set_guard_active(0);
    g_open = 0;
    g_dirty = 0;
    g_dd_open = 0;
    g_submenu = 0;
    g_ref_idx = 4;
    a7r3_dsb();
    for (spin = 0; spin < 50 && g_drawing; spin++)
        a7r3_nanosleep_ms(1u);
    if (g_native_ok) {
        a7r3_fb_restore_saved();
        g_native_ok = 0;
    }
}

static void sync_item_action(int row)
{
    if (row == 0)
        a7r3_input_set_ev_dial_shield(g_item_vals[0]);
    else if (row == 1)
        a7r3_dual_hud_set_enabled(g_item_vals[1]);
    else if (row == 2)
        a7r3_pb_focus_set_enabled(g_item_vals[2]);
}

static void reset_defaults(void)
{
    g_item_vals[0] = 0;
    g_item_vals[1] = 0;
    g_item_vals[2] = 0;
    g_theme = 0;
    g_dd_open = 0;
    sync_item_action(0);
    sync_item_action(1);
    sync_item_action(2);
    a7r3_shutter_sfx_reset();
    g_dirty = 1;
}

static int wrap_step(int cur, int delta, int count)
{
    int nxt;
    if (count <= 1)
        return 0;
    nxt = cur + delta;
    if (nxt < 0)
        nxt = count - 1;
    else if (nxt >= count)
        nxt = 0;
    return nxt;
}

static void switch_page(int delta, int focus_pag)
{
    int rows;
    g_dd_open = 0;
    g_page = wrap_step(g_page, delta, PAGE_CNT);
    rows = page_row_count(g_page);
    g_sel = (focus_pag || rows == 0) ? rows : 0;
    g_dirty = 1;
}

static void handle_events(void)
{
    struct a7r3_key_ev kev;
    struct a7r3_touch_ev tev;
    int settle = ((int)(a7r3_now_ms() - g_open_ms) < 120);

    while (a7r3_input_poll_key(&kev)) {
        int rows = page_row_count(g_page);
        int focus_slots = rows + 2;
        if (!g_open || settle || kev.status != 1u)
            continue;
        if (g_submenu == 1) {
            switch (kev.code) {
            case A7R3_KEY_UP:
                g_sfx_sel = wrap_step(g_sfx_sel, -1, SFX_SLOTS);
                g_dirty = 1;
                break;
            case A7R3_KEY_DOWN:
                g_sfx_sel = wrap_step(g_sfx_sel, 1, SFX_SLOTS);
                g_dirty = 1;
                break;
            case A7R3_KEY_LEFT:
                if (g_sfx_sel == 0) {
                    a7r3_shutter_sfx_set_enabled(0);
                    g_dirty = 1;
                } else if (g_sfx_sel == 1) {
                    int cnt = a7r3_shutter_sfx_count();
                    int nxt = wrap_step(a7r3_shutter_sfx_get_selected(), -1, cnt);
                    a7r3_shutter_sfx_select(nxt);
                    g_dirty = 1;
                } else if (g_sfx_sel == 2) {
                    int v = a7r3_shutter_sfx_get_volume();
                    a7r3_shutter_sfx_set_volume(wrap_step(v, -1, 3));
                    g_dirty = 1;
                } else if (g_sfx_sel == 3) {
                    a7r3_shutter_sfx_preview();
                    g_dirty = 1;
                } else if (g_sfx_sel == 4) {
                    g_sfx_sel = 5;
                    g_dirty = 1;
                } else {
                    g_submenu = 0;
                    g_dirty = 1;
                }
                break;
            case A7R3_KEY_RIGHT:
                if (g_sfx_sel == 0) {
                    a7r3_shutter_sfx_set_enabled(1);
                    g_dirty = 1;
                } else if (g_sfx_sel == 1) {
                    int cnt = a7r3_shutter_sfx_count();
                    int nxt = wrap_step(a7r3_shutter_sfx_get_selected(), 1, cnt);
                    a7r3_shutter_sfx_select(nxt);
                    g_dirty = 1;
                } else if (g_sfx_sel == 2) {
                    int v = a7r3_shutter_sfx_get_volume();
                    a7r3_shutter_sfx_set_volume(wrap_step(v, 1, 3));
                    g_dirty = 1;
                } else if (g_sfx_sel == 3) {
                    a7r3_shutter_sfx_preview();
                    g_dirty = 1;
                } else if (g_sfx_sel == 4) {
                    a7r3_shutter_sfx_rescan_sd();
                    g_sfx_scanned_once = 1;
                    g_dirty = 1;
                } else {
                    g_sfx_sel = 4;
                    g_dirty = 1;
                }
                break;
            case A7R3_KEY_ENTER:
                if (g_sfx_sel == 0) {
                    a7r3_shutter_sfx_set_enabled(!a7r3_shutter_sfx_get_enabled());
                    g_dirty = 1;
                } else if (g_sfx_sel == 1) {
                    int cnt = a7r3_shutter_sfx_count();
                    int nxt = wrap_step(a7r3_shutter_sfx_get_selected(), 1, cnt);
                    a7r3_shutter_sfx_select(nxt);
                    g_dirty = 1;
                } else if (g_sfx_sel == 2) {
                    int v = a7r3_shutter_sfx_get_volume();
                    a7r3_shutter_sfx_set_volume(wrap_step(v, 1, 3));
                    g_dirty = 1;
                } else if (g_sfx_sel == 3) {
                    a7r3_shutter_sfx_preview();
                    g_dirty = 1;
                } else if (g_sfx_sel == 4) {
                    a7r3_shutter_sfx_rescan_sd();
                    g_sfx_scanned_once = 1;
                    g_dirty = 1;
                } else {
                    g_submenu = 0;
                    g_dirty = 1;
                }
                break;
            case A7R3_KEY_MENU:
                g_submenu = 0;
                g_dirty = 1;
                break;
            default:
                break;
            }
            continue;
        }
        if (g_dd_open) {
            switch (kev.code) {
            case A7R3_KEY_UP:
                g_dd_sel = wrap_step(g_dd_sel, -1, 3);
                g_dirty = 1;
                break;
            case A7R3_KEY_DOWN:
                g_dd_sel = wrap_step(g_dd_sel, 1, 3);
                g_dirty = 1;
                break;
            case A7R3_KEY_ENTER:
            case A7R3_KEY_RIGHT:
                g_theme = g_dd_sel;
                g_dd_open = 0;
                g_dirty = 1;
                break;
            case A7R3_KEY_LEFT:
            case A7R3_KEY_MENU:
                g_dd_open = 0;
                g_dirty = 1;
                break;
            default:
                break;
            }
            continue;
        }
        switch (kev.code) {
        case A7R3_KEY_UP:
            g_sel = wrap_step(g_sel, -1, focus_slots);
            g_dirty = 1;
            break;
        case A7R3_KEY_DOWN:
            g_sel = wrap_step(g_sel, 1, focus_slots);
            g_dirty = 1;
            break;
        case A7R3_KEY_LEFT:
            if (g_sel == rows) {
                switch_page(-1, 1);
            } else if (g_sel == rows + 1) {
                g_theme = wrap_step(g_theme, -1, 3);
                g_dirty = 1;
            } else if (g_sel == 3) {
                enter_sfx_submenu();
            } else {
                g_item_vals[g_sel] = 0;
                sync_item_action(g_sel);
                g_dirty = 1;
            }
            break;
        case A7R3_KEY_RIGHT:
            if (g_sel == rows) {
                switch_page(1, 1);
            } else if (g_sel == rows + 1) {
                g_theme = wrap_step(g_theme, 1, 3);
                g_dirty = 1;
            } else if (g_sel == 3) {
                enter_sfx_submenu();
            } else {
                g_item_vals[g_sel] = 1;
                sync_item_action(g_sel);
                g_dirty = 1;
            }
            break;
        case A7R3_KEY_ENTER:
            if (g_sel == rows) {
                switch_page(1, 1);
            } else if (g_sel == rows + 1) {
                g_dd_open = 1;
                g_dd_sel = g_theme;
                g_dirty = 1;
            } else if (g_sel == 3) {
                enter_sfx_submenu();
            } else {
                g_item_vals[g_sel] ^= 1;
                sync_item_action(g_sel);
                g_dirty = 1;
            }
            break;
        case A7R3_KEY_MENU:
            close_addon();
            break;
        default:
            break;
        }
    }

    while (a7r3_input_poll_touch(&tev)) {
        int rows = page_row_count(g_page);
        int r;
        if (!tev.down || !g_open || settle)
            continue;
        if (g_dd_open) {
            if (tev.x >= DD_MENU_X && tev.x < DD_MENU_X + DD_MENU_W &&
                tev.y >= DD_MENU_Y && tev.y < DD_MENU_Y + DD_MENU_H) {
                int idx = 0;
                for (idx = 0; idx < 2; idx++) {
                    if (tev.y < DD_MENU_Y + DD_MENU_PAD + (idx + 1) * DD_ITEM_H)
                        break;
                }
                g_theme = idx;
                g_dd_sel = idx;
            }
            g_dd_open = 0;
            g_dirty = 1;
            continue;
        }
        if (tev.x < CARD_X || tev.x >= CARD_X + CARD_W ||
            tev.y < CARD_Y || tev.y >= CARD_Y + CARD_H) {
            close_addon();
            continue;
        }
        if (g_submenu == 1) {
            if (tev.x >= SFX_BACK_X - 6 && tev.x < SFX_BACK_X + SFX_BACK_W + 6 &&
                tev.y >= SFX_BACK_Y - 6 && tev.y < SFX_BACK_Y + SFX_BACK_H + 6) {
                g_submenu = 0;
                g_dirty = 1;
                continue;
            }
            if (tev.y >= BTN_Y - 4 && tev.y < BTN_Y + BTN_H + 4) {
                if (tev.x >= BTN_BACK_X - 4 && tev.x < BTN_BACK_X + BTN_BACK_W + 4) {
                    g_submenu = 0;
                    g_dirty = 1;
                    continue;
                }
                if (tev.x >= BTN_RESCAN_X - 4 && tev.x < BTN_RESCAN_X + BTN_RESCAN_W + 4) {
                    g_sfx_sel = 4;
                    a7r3_shutter_sfx_rescan_sd();
                    g_sfx_scanned_once = 1;
                    g_dirty = 1;
                    continue;
                }
                if (tev.x >= BTN_DON_X && tev.x < BTN_DON_X + BTN_DON_W) {
                    close_addon();
                    continue;
                }
            }
            for (r = 0; r < SFX_CNT; r++) {
                int ry = ROW_Y0 + r * ROW_STEP;
                if (tev.x >= ROW_X && tev.x < ROW_X + ROW_W &&
                    tev.y >= ry && tev.y < ry + ROW_H) {
                    g_sfx_sel = r;
                    if (r == 0) {
                        a7r3_shutter_sfx_set_enabled(!a7r3_shutter_sfx_get_enabled());
                    } else if (r == 1) {
                        int cnt = a7r3_shutter_sfx_count();
                        int delta = (tev.x >= SFX_SEL_X && tev.x < SFX_SEL_X + 40) ? -1 : 1;
                        a7r3_shutter_sfx_select(wrap_step(a7r3_shutter_sfx_get_selected(), delta, cnt));
                    } else if (r == 2) {
                        int c;
                        int handled = 0;
                        for (c = 0; c < 3; c++) {
                            int cx = SFX_VOL_X0 + c * (SFX_VOL_W + SFX_VOL_GAP);
                            if (tev.x >= cx - 2 && tev.x < cx + SFX_VOL_W + 2) {
                                a7r3_shutter_sfx_set_volume(c);
                                handled = 1;
                                break;
                            }
                        }
                        if (!handled) {
                            int v = a7r3_shutter_sfx_get_volume();
                            a7r3_shutter_sfx_set_volume(wrap_step(v, 1, 3));
                        }
                    } else {
                        a7r3_shutter_sfx_preview();
                    }
                    g_dirty = 1;
                    break;
                }
            }
            continue;
        }
        if (tev.x >= DD_LBL_X - 4 && tev.x < DD_X + DD_W + 4 &&
            tev.y >= DD_Y - 6 && tev.y < DD_Y + DD_H + 6) {
            g_sel = rows + 1;
            g_dd_open = 1;
            g_dd_sel = g_theme;
            g_dirty = 1;
            continue;
        }
        if (tev.y >= BTN_Y - 4 && tev.y < BTN_Y + BTN_H + 4) {
            if (tev.x >= BTN_PREV_X - 4 && tev.x < BTN_PREV_X + BTN_PREV_W + 4) {
                switch_page(-1, 1);
                continue;
            }
            if (tev.x >= BTN_NEXT_X - 4 && tev.x < BTN_NEXT_X + BTN_NEXT_W + 4) {
                switch_page(1, 1);
                continue;
            }
            if (tev.x >= BTN_RST_X && tev.x < BTN_RST_X + BTN_RST_W) {
                reset_defaults();
                continue;
            }
            if (tev.x >= BTN_DON_X && tev.x < BTN_DON_X + BTN_DON_W) {
                close_addon();
                continue;
            }
        }
        for (r = 0; r < rows; r++) {
            int ry = ROW_Y0 + r * ROW_STEP;
            if (tev.x >= ROW_X && tev.x < ROW_X + ROW_W &&
                tev.y >= ry && tev.y < ry + ROW_H) {
                g_sel = r;
                if (r == 3) {
                    enter_sfx_submenu();
                } else {
                    g_item_vals[r] ^= 1;
                    sync_item_action(r);
                    g_dirty = 1;
                }
                break;
            }
        }
    }
}

static void *addon_worker(void *arg)
{
    (void)arg;

    a7r3_log_init("/tmp_bt/addon.log");
    a7r3_log_str("SDK_CERT_ALIVE\n");
    a7r3_maps_reload();
    if (a7r3_fb_open() != 0) {
        a7r3_log_str("SDK_FB_FAIL\n");
        return NULL;
    }
    a7r3_fb_install_ioctl_hook();
    a7r3_input_install();
    a7r3_dual_hud_install();
    a7r3_pb_focus_install();
    a7r3_shutter_sfx_install();
    a7r3_shutter_sfx_refresh_state();
    a7r3_input_set_ext_dismiss_cb(on_external_dismiss);
    if (a7r3_entry_cert_install() != 0)
        a7r3_log_str("SDK_CERT_INSTALL_ERR\n");

    {
        unsigned last_gate_ms = 0u;
        unsigned last_act_seq = a7r3_input_native_activity_seq();
        unsigned last_act_ms = 0u;
        int act_pending = 0;

        for (;;) {
            unsigned now;
            unsigned cur_act;
            unsigned ec = 0, es = 0, dc = 0, ds = 0;
            unsigned evc = 0, evw1 = 0, evw2 = 0;
            unsigned dp = 0, dg = 0, dr = 0;
            unsigned long pa = 0;

            a7r3_nanosleep_ms(g_open ? 5u : 25u);
            now = a7r3_now_ms();

            if ((unsigned)(now - last_gate_ms) >= 200u) {
                last_gate_ms = now;
                if (a7r3_file_exists(GATE_QUIT)) {
                    close_addon();
                    a7r3_input_set_ev_dial_shield(0);
                    a7r3_dual_hud_set_enabled(0);
                    a7r3_pb_focus_set_enabled(0);
                    a7r3_shutter_sfx_set_enabled(0);
                    a7r3_entry_cert_remove();
                    a7r3_input_set_ext_dismiss_cb(NULL);
                    a7r3_shutter_sfx_uninstall();
                    a7r3_pb_focus_remove();
                    a7r3_dual_hud_remove();
                    a7r3_input_remove();
                    a7r3_fb_remove_ioctl_hook();
                    a7r3_fb_close();
                    a7r3_log_str("SDK_CERT_QUIT\n");
                    return NULL;
                }
                if (a7r3_file_exists(GATE_OPEN)) {
                    a7r3_unlink(GATE_OPEN);
                    open_addon();
                }
            }

            if (!g_open) {
                cur_act = a7r3_input_native_activity_seq();
                if (cur_act != last_act_seq) {
                    last_act_seq = cur_act;
                    last_act_ms = now;
                    act_pending = 1;
                } else if (act_pending && (unsigned)(now - last_act_ms) >= 700u) {
                    act_pending = 0;
                    a7r3_shutter_sfx_refresh_state();
                }
            } else {
                act_pending = 0;
            }

            if (a7r3_input_take_ext_dismiss(&ec, &es)) {
                a7r3_log_hex("SDK_MD3_EXT_CODE=", (unsigned long)ec);
                a7r3_log_hex("SDK_MD3_EXT_STAT=", (unsigned long)es);
            }
            if (a7r3_input_take_disp_switch(&dc, &ds)) {
                a7r3_log_hex("SDK_DISP_SW=", (unsigned long)dc);
                a7r3_log_hex("SDK_DISP_ST=", (unsigned long)ds);
                if (g_open) {
                    a7r3_fb_arm_pa_probe(24u);
                    g_ref_deadlines[0] = now + 80u;
                    g_ref_deadlines[1] = now + 200u;
                    g_ref_deadlines[2] = now + 400u;
                    g_ref_deadlines[3] = now + 700u;
                    g_ref_idx = 0;
                    g_dirty = 1;
                }
            }
            if (a7r3_input_take_ev_raw(&evc, &evw1, &evw2)) {
                a7r3_log_hex("SDK_EV_RAW_CODE=", (unsigned long)evc);
                a7r3_log_hex("SDK_EV_RAW_W1=", (unsigned long)evw1);
                a7r3_log_hex("SDK_EV_RAW_W2=", (unsigned long)evw2);
            }
            if (a7r3_dual_hud_take_stats(&dp, &dg, &dr)) {
                a7r3_log_hex("SDK_DUAL_PARSE=", (unsigned long)dp);
                a7r3_log_hex("SDK_DUAL_GATE1=", (unsigned long)dg);
                a7r3_log_hex("SDK_DUAL_ROUTE=", (unsigned long)dr);
            }
            while (a7r3_fb_pop_probed_pa(&pa))
                a7r3_log_hex("SDK_GPU_PA=", pa);
            if (a7r3_entry_cert_take_triggered())
                open_addon();
            {
                unsigned lh = 0;
                if (a7r3_entry_cert_take_label_hits(&lh))
                    a7r3_log_hex("SDK_CERT_LABEL_HITS=", (unsigned long)lh);
            }

            handle_events();
            a7r3_pb_focus_tick(g_open);
            if (g_open && g_submenu == 1) {
                int cur_eshut = a7r3_shutter_sfx_is_eshutter();
                int cur_sd = a7r3_shutter_sfx_sd_count();
                if (cur_eshut != g_last_eshut) {
                    g_last_eshut = cur_eshut;
                    g_dirty = 1;
                }
                if (cur_sd != g_last_sd_cnt) {
                    g_last_sd_cnt = cur_sd;
                    g_dirty = 1;
                }
            }
            if (g_open) {
                g_drawing = 1;
                a7r3_dsb();
                if (g_open) {
                    if (g_dirty) {
                        render_addon_card();
                    } else if (g_ref_idx < 4 &&
                               (int)(a7r3_now_ms() - g_ref_deadlines[g_ref_idx]) >= 0) {
                        g_ref_idx++;
                        a7r3_fb_present_full();
                        a7r3_log_hex("SDK_DISP_REPRES=", (unsigned long)g_ref_idx);
                    } else {
                        a7r3_fb_repair_tick();
                    }
                }
                g_drawing = 0;
                a7r3_dsb();
            }
        }
    }
}

__attribute__((constructor)) static void demo_cert_ctor(void)
{
    unsigned long tid = 0;
    int (*volatile pcreate)(unsigned long *, const void *,
                            void *(*)(void *), void *) = pthread_create;
    if (!a7r3_comm_is_im())
        return;
    if (!a7r3_file_exists(GATE_ARM) || a7r3_file_exists(GATE_OFF))
        return;
    if (pcreate)
        pcreate(&tid, NULL, addon_worker, NULL);
}
