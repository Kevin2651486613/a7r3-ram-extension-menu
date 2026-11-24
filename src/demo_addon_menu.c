#include "a7r3_sys.h"
#include "a7r3_fb.h"
#include "a7r3_draw.h"
#include "a7r3_input.h"

#define GATE_ARM  "/tmp_bt/addon_arm"
#define GATE_OFF  "/tmp_bt/addon_off"
#define GATE_OPEN "/tmp_bt/addon_open"
#define GATE_QUIT "/tmp_bt/addon_quit"

#define BADGE_X   590
#define BADGE_Y   6
#define BADGE_W   44
#define BADGE_H   44

#define CARD_X    90
#define CARD_Y    78
#define CARD_W    460
#define CARD_H    324
#define ROW_Y0    (CARD_Y + 56)
#define ROW_H     50
#define ITEM_CNT  4

extern int pthread_create(unsigned long *tid, const void *attr,
                          void *(*fn)(void *), void *arg)
    __attribute__((weak));

static uint16_t g_canvas[A7R3_FB_PIXELS];
static uint16_t g_native_bg[A7R3_FB_PIXELS];
static int g_native_ok;

static int g_open;
static int g_dirty;
static int g_sel;
static int g_item_vals[ITEM_CNT] = { 1, 0, 1, 0 };

static const char *const g_item_names[ITEM_CNT] = {
    "Custom Focus Aid",
    "Peak Overlay Mode",
    "Bracket Shortcut",
    "Extra Log Telemetry"
};

static void draw_corner_badge(int active)
{
    uint16_t bg = active ? A7R3_RGB888(215, 90, 24) : A7R3_RGB888(28, 32, 42);
    uint16_t bd = active ? A7R3_RGB888(255, 200, 120) : A7R3_RGB888(120, 140, 170);
    uint16_t fg = A7R3_RGB888(255, 255, 255);

    a7r3_draw_fill_rect(g_canvas, BADGE_X, BADGE_Y, BADGE_W, BADGE_H, bg);
    a7r3_draw_stroke_rect(g_canvas, BADGE_X, BADGE_Y, BADGE_W, BADGE_H, 2, bd);
    a7r3_draw_text_ascii(g_canvas, BADGE_X + 9, BADGE_Y + 15, "EX", fg, 2);
}

static void render_addon_card(void)
{
    unsigned i;
    int r;

    if (g_native_ok) {
        for (i = 0; i < A7R3_FB_PIXELS; i++)
            g_canvas[i] = g_native_bg[i];
        a7r3_draw_dim_rect(g_canvas, 0, 0, (int)A7R3_FB_W, (int)A7R3_FB_H);
    } else {
        a7r3_draw_clear(g_canvas, A7R3_RGB888(14, 16, 22));
    }

    draw_corner_badge(1);

    /* Floating card container */
    a7r3_draw_fill_rect(g_canvas, CARD_X, CARD_Y, CARD_W, CARD_H, A7R3_RGB888(24, 28, 38));
    a7r3_draw_stroke_rect(g_canvas, CARD_X, CARD_Y, CARD_W, CARD_H, 2, A7R3_RGB888(110, 130, 165));
    a7r3_draw_fill_rect(g_canvas, CARD_X + 2, CARD_Y + 2, CARD_W - 4, 42, A7R3_RGB888(36, 44, 62));
    a7r3_draw_text_ascii(g_canvas, CARD_X + 18, CARD_Y + 15, "A7R3 ADD-ON MENU",
                         A7R3_RGB888(255, 255, 255), 2);

    for (r = 0; r < ITEM_CNT; r++) {
        int ry = ROW_Y0 + r * ROW_H;
        int sel = (r == g_sel);
        uint16_t row_bg = sel ? A7R3_RGB888(210, 88, 22) : A7R3_RGB888(34, 38, 50);
        uint16_t txt_fg = sel ? A7R3_RGB888(255, 255, 255) : A7R3_RGB888(215, 220, 232);
        uint16_t val_fg = g_item_vals[r]
            ? A7R3_RGB888(130, 235, 160)
            : A7R3_RGB888(170, 175, 190);

        a7r3_draw_fill_rect(g_canvas, CARD_X + 14, ry, CARD_W - 28, ROW_H - 6, row_bg);
        a7r3_draw_text_ascii(g_canvas, CARD_X + 26, ry + 15, g_item_names[r], txt_fg, 2);
        a7r3_draw_text_ascii(g_canvas, CARD_X + CARD_W - 72, ry + 15,
                             g_item_vals[r] ? "ON" : "OFF",
                             sel ? txt_fg : val_fg, 2);
    }

    a7r3_draw_text_ascii(g_canvas, CARD_X + 18, CARD_Y + CARD_H - 28,
                         "UP/DN:Move  ENTER:Toggle  MENU:Close",
                         A7R3_RGB888(160, 172, 195), 1);

    a7r3_fb_pack_frame(g_canvas);
    a7r3_fb_present_full();
    g_dirty = 0;
}

static void open_addon(void)
{
    if (g_open)
        return;
    g_native_ok = (a7r3_fb_grab_native(g_native_bg) == 0);
    g_open = 1;
    g_dirty = 1;
    a7r3_input_set_swallow(1, 1);
    a7r3_fb_set_guard_active(1);
    a7r3_log_str("SDK_OPEN\n");
}

static void close_addon(void)
{
    if (!g_open)
        return;
    g_open = 0;
    g_dirty = 0;
    a7r3_fb_set_guard_active(0);
    a7r3_input_set_swallow(0, 0);
    if (g_native_ok) {
        a7r3_fb_restore_native(g_native_bg);
        g_native_ok = 0;
    }
    draw_corner_badge(0);
    a7r3_fb_blit_rect(BADGE_X, BADGE_Y, BADGE_W, BADGE_H, g_canvas, (int)A7R3_FB_W);
    a7r3_log_str("SDK_CLOSE\n");
}

static void handle_events(void)
{
    struct a7r3_key_ev kev;
    struct a7r3_touch_ev tev;

    while (a7r3_input_poll_key(&kev)) {
        if (!g_open || kev.status != 1u)
            continue;
        switch (kev.code) {
        case A7R3_KEY_UP:
            g_sel = (g_sel + ITEM_CNT - 1) % ITEM_CNT;
            g_dirty = 1;
            break;
        case A7R3_KEY_DOWN:
            g_sel = (g_sel + 1) % ITEM_CNT;
            g_dirty = 1;
            break;
        case A7R3_KEY_ENTER:
            g_item_vals[g_sel] ^= 1;
            g_dirty = 1;
            break;
        case A7R3_KEY_MENU:
            close_addon();
            break;
        default:
            break;
        }
    }

    while (a7r3_input_poll_touch(&tev)) {
        if (!tev.down)
            continue;
        if (tev.x >= BADGE_X - 6 && tev.y <= BADGE_Y + BADGE_H + 6) {
            if (g_open)
                close_addon();
            else
                open_addon();
            continue;
        }
        if (!g_open)
            continue;
        if (tev.x < CARD_X || tev.x >= CARD_X + CARD_W ||
            tev.y < CARD_Y || tev.y >= CARD_Y + CARD_H) {
            close_addon();
            continue;
        }
        if (tev.y >= ROW_Y0 && tev.y < ROW_Y0 + ITEM_CNT * ROW_H) {
            int row = (tev.y - ROW_Y0) / ROW_H;
            if (row == g_sel)
                g_item_vals[row] ^= 1;
            else
                g_sel = row;
            g_dirty = 1;
        }
    }
}

static void *addon_worker(void *arg)
{
    unsigned beat = 0;
    (void)arg;

    a7r3_log_init("/tmp_bt/addon.log");
    a7r3_log_str("SDK_ALIVE\n");
    a7r3_maps_reload();
    if (a7r3_fb_open() != 0) {
        a7r3_log_str("SDK_FB_FAIL\n");
        return NULL;
    }
    a7r3_fb_install_ioctl_hook();
    a7r3_input_install();

    for (;;) {
        a7r3_nanosleep_ms(5u);
        if (a7r3_file_exists(GATE_QUIT)) {
            close_addon();
            a7r3_input_remove();
            a7r3_fb_remove_ioctl_hook();
            a7r3_fb_close();
            a7r3_log_str("SDK_QUIT\n");
            return NULL;
        }
        if (a7r3_file_exists(GATE_OPEN)) {
            a7r3_unlink(GATE_OPEN);
            open_addon();
        }
        handle_events();
        if (g_open) {
            if (g_dirty)
                render_addon_card();
            else
                a7r3_fb_repair_tick();
        } else if ((beat % 20u) == 0u) {
            draw_corner_badge(0);
            a7r3_fb_blit_rect(BADGE_X, BADGE_Y, BADGE_W, BADGE_H,
                              g_canvas, (int)A7R3_FB_W);
        }
        beat++;
    }
}

__attribute__((constructor)) static void demo_addon_ctor(void)
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
