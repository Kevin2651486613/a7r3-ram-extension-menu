#include "a7r3_menu_touch.h"
#include "a7r3_input.h"
#include "a7r3_sys.h"

/* Verified A7R3 v3.10 viewUnified2.so RW vtable slots & targets */
#define VU2_CAUTION_WORD_OFF      0x00a6cd2cUL /* (word & 0x4000u) != 0 -> caution active */

#define VU2_VIEW_SETTING_VPTR     0x00863890UL
#define VU2_VIEW_SETTING_D1_SLOT  0x008638b0UL /* +0x020 */
#define VU2_VIEW_SETTING_D0_SLOT  0x008638b4UL /* +0x024 */
#define VU2_VIEW_SETTING_EV_SLOT  0x0086397cUL /* +0x0ec */
#define VU2_VIEW_SETTING_D1_REAL  0x00207515UL /* Thumb */
#define VU2_VIEW_SETTING_D0_REAL  0x00207581UL /* Thumb */
#define VU2_VIEW_SETTING_EV_REAL  0x00210b31UL /* Thumb */

#define VU2_MLT_VPTR              0x008bc9d0UL
#define VU2_MLT_NAV_SLOT          0x008bcb80UL /* +0x1b0 */
#define VU2_MLT_CAT_SLOT          0x008bcb84UL /* +0x1b4 */
#define VU2_MLT_NAV_REAL          0x0056e69bUL /* Thumb */
#define VU2_MLT_CAT_REAL          0x0056e653UL /* Thumb */

#define VU2_CAT_VPTR              0x008bf088UL
#define VU2_CAT_UPD_SLOT          0x008bf234UL /* +0x1ac */
#define VU2_CAT_NAV_SLOT          0x008bf238UL /* +0x1b0 */
#define VU2_CAT_UPD_REAL          0x0057dcc9UL /* Thumb */
#define VU2_CAT_NAV_REAL          0x0057d5cdUL /* Thumb */

#define VU2_GRP_VPTR              0x008b41e0UL
#define VU2_GRP_UPD_SLOT          0x008b438cUL /* +0x1ac */
#define VU2_GRP_NAV_SLOT          0x008b4390UL /* +0x1b0 */
#define VU2_GRP_UPD_REAL          0x0053871dUL /* Thumb */
#define VU2_GRP_NAV_REAL          0x005384f5UL /* Thumb */

#define VU2_BELT_VPTR             0x008b3ad0UL
#define VU2_POPUP_GET_CNT_REAL    0x0053be95UL /* Thumb: arg = mlt + 0x0f7c8 */
#define VU2_MLT_POPUP_OFF         0x0f7c8u
#define VU2_POPUP_MAX_VIS_OFF     0x15ac8u     /* mlt + 0x0f7c8 + 0x6300 */
#define VU2_POPUP_TOP_OFF         0x15ad4u
#define VU2_POPUP_OPEN_OFF        0x15ad6u

/* Verified A7R3 v3.10 viewUnified7.so (White Balance dedicated page) */
#define VU7_VIEW_WB_VPTR          0x0007dd10UL
#define VU7_VIEW_WB_D1_SLOT       0x0007dd30UL /* +0x020 */
#define VU7_VIEW_WB_D0_SLOT       0x0007dd34UL /* +0x024 */
#define VU7_VIEW_WB_EV_SLOT       0x0007ddfcUL /* +0x0ec */
#define VU7_VIEW_WB_D1_REAL       0x0002a6f9UL /* Thumb */
#define VU7_VIEW_WB_D0_REAL       0x0002a741UL /* Thumb */
#define VU7_VIEW_WB_EV_REAL       0x0002d851UL /* Thumb */

struct mlt_nav_out {
    int cat;
    int page;
    int row;
    int pad;
    uint8_t ok;
    uint8_t pad2[3];
};

struct sub_nav_out {
    int page;
    int row;
    int pad;
    uint8_t ok;
    uint8_t pad2[3];
};

typedef void *(*view_dtor_fn)(void *self);
typedef int   (*view_event_fn)(void *self, void *ev, void *arg);
typedef struct mlt_nav_out *(*mlt_nav_fn)(struct mlt_nav_out *out, void *self, int dir);
typedef struct mlt_nav_out *(*mlt_cat_fn)(struct mlt_nav_out *out, void *self);
typedef int   (*wgt_upd_fn)(void *self);
typedef struct sub_nav_out *(*sub_nav_fn)(struct sub_nav_out *out, void *self, int dir);
typedef int   (*popup_cnt_fn)(void *popup_sub);

struct slot_hook {
    volatile unsigned long *slot;
    unsigned long orig;
};

static int g_mt_installed;
static int g_wb_hooked;
static volatile int g_menu_touch_enabled;
static unsigned long g_vu2_base;
static unsigned long g_vu7_base;

/* Saved RW vtable slots for atomic uninstall */
static struct slot_hook g_hk_vsm_d1, g_hk_vsm_d0, g_hk_vsm_ev;
static struct slot_hook g_hk_mlt_nav, g_hk_mlt_cat;
static struct slot_hook g_hk_cat_upd, g_hk_cat_nav;
static struct slot_hook g_hk_grp_upd, g_hk_grp_nav;
static struct slot_hook g_hk_wb_d1, g_hk_wb_d0, g_hk_wb_ev;

static view_dtor_fn  g_real_vsm_d1, g_real_vsm_d0;
static view_event_fn g_real_vsm_ev;
static mlt_nav_fn    g_real_mlt_nav;
static mlt_cat_fn    g_real_mlt_cat;
static wgt_upd_fn    g_real_cat_upd, g_real_grp_upd;
static sub_nav_fn    g_real_cat_nav, g_real_grp_nav;
static view_dtor_fn  g_real_wb_d1, g_real_wb_d0;
static view_event_fn g_real_wb_ev;
static popup_cnt_fn  g_popup_get_cnt;

/* Live Sony UI instance trackers (updated synchronously on Sony's UI thread) */
static void *volatile g_cur_setting_view;
static void *volatile g_cur_mlt;
static void *volatile g_cur_cat_wgt;
static void *volatile g_cur_grp_wgt;
static void *volatile g_cur_wb_view;
static volatile int g_cur_mlt_row_cnt = -1;
static volatile int g_cur_popup_cnt = -1;

/* One-shot target overrides for 1-step native row / category jumps */
static volatile int g_req_mlt_cat;      /* 1..7, 0 = inactive */
static volatile int g_req_mlt_row = -1; /* 0..5, -1 = inactive */
static volatile int g_req_cat_row = -1; /* 0..5, -1 = inactive */
static volatile int g_req_grp_row = -1; /* 0..5, -1 = inactive */

/* Diagnostics & bounce filter */
static volatile unsigned g_mt_taps;
static volatile unsigned g_mt_actions;
static unsigned g_last_mt_taps;
static unsigned g_last_mt_actions;
static unsigned g_last_tap_ms;
static int g_last_tap_x = -999;
static int g_last_tap_y = -999;

/* =========================================================================
 * Hook Helpers (Validate RW GOT/vtable slot before writing & restore cleanly)
 * ========================================================================= */

static int hook_vtable_slot(unsigned long mod_base, unsigned long slot_off,
                            unsigned long expected_off, struct slot_hook *rec,
                            void *hook_fn, void **out_real)
{
    volatile unsigned long *slot = (volatile unsigned long *)(mod_base + slot_off);
    unsigned long expected = mod_base + expected_off;
    if (!a7r3_pm_writable((const void *)slot, 4u))
        return -1;
    if (*slot != expected)
        return -2;
    rec->orig = *slot;
    rec->slot = slot;
    if (out_real)
        *out_real = (void *)(uintptr_t)expected;
    a7r3_dsb();
    *slot = (unsigned long)(uintptr_t)hook_fn;
    a7r3_dsb();
    return 0;
}

static void unhook_vtable_slot(struct slot_hook *rec)
{
    if (rec->slot && a7r3_pm_writable((const void *)rec->slot, 4u)) {
        *rec->slot = rec->orig;
        a7r3_dsb();
    }
    rec->slot = NULL;
    rec->orig = 0UL;
}

static void clear_setting_refs(void *self)
{
    if (g_cur_setting_view == self) {
        g_cur_setting_view = NULL;
        g_cur_mlt = NULL;
        g_cur_cat_wgt = NULL;
        g_cur_grp_wgt = NULL;
        g_cur_mlt_row_cnt = -1;
        g_cur_popup_cnt = -1;
        g_req_mlt_cat = 0;
        g_req_mlt_row = -1;
        g_req_cat_row = -1;
        g_req_grp_row = -1;
        a7r3_dsb();
    }
}

/* =========================================================================
 * Sony UI Thread Hooks (Lifecycle Tracking & 1-Step Target Computation)
 * ========================================================================= */

__attribute__((noinline))
static void *hook_vsm_d1(void *self)
{
    clear_setting_refs(self);
    return g_real_vsm_d1(self);
}

__attribute__((noinline))
static void *hook_vsm_d0(void *self)
{
    clear_setting_refs(self);
    return g_real_vsm_d0(self);
}

__attribute__((noinline))
static int hook_vsm_ev(void *self, void *ev, void *arg)
{
    int rc;
    g_cur_setting_view = self;
    a7r3_dsb();
    rc = g_real_vsm_ev(self, ev, arg);
    if (self && a7r3_pm_readable(self, 0x148u)) {
        const uint8_t *vp = (const uint8_t *)self;
        int state = *(const int *)(vp + 0x04u);
        int level = *(const int *)(vp + 0x130u);
        void *mlt = g_cur_mlt;
        if (state == 1 && level == 2 && mlt && g_popup_get_cnt &&
            a7r3_pm_readable(mlt, VU2_POPUP_OPEN_OFF + 8u) &&
            *(const unsigned long *)mlt == (g_vu2_base + VU2_MLT_VPTR) &&
            *(const uint8_t *)((const uint8_t *)mlt + VU2_POPUP_OPEN_OFF) == 1u) {
            int cnt = g_popup_get_cnt((uint8_t *)mlt + VU2_MLT_POPUP_OFF);
            if (cnt >= 1 && cnt <= 64)
                g_cur_popup_cnt = cnt;
        } else if (state != 1 || level != 2) {
            g_cur_popup_cnt = -1;
        }
    }
    return rc;
}

__attribute__((noinline))
static struct mlt_nav_out *hook_mlt_nav(struct mlt_nav_out *out, void *self, int dir)
{
    struct mlt_nav_out *ret = g_real_mlt_nav(out, self, dir);
    int want = g_req_mlt_row;
    if (want >= 0 && want <= 5 && ret && self) {
        ret->cat  = *(const int *)((const uint8_t *)self + 0x1d4u);
        ret->page = *(const int *)((const uint8_t *)self + 0x1d8u);
        ret->row  = want;
        ret->ok   = 1u;
        *((uint8_t *)self + 0x1bcu) = 0u;
        g_req_mlt_row = -1;
        a7r3_dsb();
    }
    return ret;
}

__attribute__((noinline))
static struct mlt_nav_out *hook_mlt_cat(struct mlt_nav_out *out, void *self)
{
    struct mlt_nav_out *ret = g_real_mlt_cat(out, self);
    int want = g_req_mlt_cat;
    if (want >= 1 && want <= 7 && ret && self) {
        ret->cat  = want;
        ret->page = 1;
        ret->row  = -1;
        ret->ok   = 1u;
        *((uint8_t *)self + 0x1e4u) = 0u;
        g_req_mlt_cat = 0;
        a7r3_dsb();
    }
    return ret;
}

__attribute__((noinline))
static int hook_cat_upd(void *self)
{
    g_cur_cat_wgt = self;
    a7r3_dsb();
    return g_real_cat_upd(self);
}

__attribute__((noinline))
static struct sub_nav_out *hook_cat_nav(struct sub_nav_out *out, void *self, int dir)
{
    struct sub_nav_out *ret = g_real_cat_nav(out, self, dir);
    int want = g_req_cat_row;
    if (want >= 0 && want <= 5 && ret && self) {
        ret->page = *(const int *)((const uint8_t *)self + 0x1ccu);
        ret->row  = want;
        ret->ok   = 1u;
        *((uint8_t *)self + 0x1d8u) = 0u;
        g_req_cat_row = -1;
        a7r3_dsb();
    }
    return ret;
}

__attribute__((noinline))
static int hook_grp_upd(void *self)
{
    g_cur_grp_wgt = self;
    a7r3_dsb();
    return g_real_grp_upd(self);
}

__attribute__((noinline))
static struct sub_nav_out *hook_grp_nav(struct sub_nav_out *out, void *self, int dir)
{
    struct sub_nav_out *ret = g_real_grp_nav(out, self, dir);
    int want = g_req_grp_row;
    if (want >= 0 && want <= 5 && ret && self) {
        ret->page = *(const int *)((const uint8_t *)self + 0x1c8u);
        ret->row  = want;
        ret->ok   = 1u;
        *((uint8_t *)self + 0x1d8u) = 0u;
        g_req_grp_row = -1;
        a7r3_dsb();
    }
    return ret;
}

__attribute__((noinline))
static void *hook_wb_d1(void *self)
{
    if (g_cur_wb_view == self) {
        g_cur_wb_view = NULL;
        a7r3_dsb();
    }
    return g_real_wb_d1(self);
}

__attribute__((noinline))
static void *hook_wb_d0(void *self)
{
    if (g_cur_wb_view == self) {
        g_cur_wb_view = NULL;
        a7r3_dsb();
    }
    return g_real_wb_d0(self);
}

__attribute__((noinline))
static int hook_wb_ev(void *self, void *ev, void *arg)
{
    g_cur_wb_view = self;
    a7r3_dsb();
    return g_real_wb_ev(self, ev, arg);
}

static void try_hook_wb(void)
{
    unsigned long wb_base;
    if (g_wb_hooked)
        return;
    wb_base = a7r3_mod_base("/viewUnified7.so");
    if (!wb_base)
        return;
    if (hook_vtable_slot(wb_base, VU7_VIEW_WB_D1_SLOT, VU7_VIEW_WB_D1_REAL,
                         &g_hk_wb_d1, (void *)&hook_wb_d1, (void **)&g_real_wb_d1) != 0 ||
        hook_vtable_slot(wb_base, VU7_VIEW_WB_D0_SLOT, VU7_VIEW_WB_D0_REAL,
                         &g_hk_wb_d0, (void *)&hook_wb_d0, (void **)&g_real_wb_d0) != 0 ||
        hook_vtable_slot(wb_base, VU7_VIEW_WB_EV_SLOT, VU7_VIEW_WB_EV_REAL,
                         &g_hk_wb_ev, (void *)&hook_wb_ev, (void **)&g_real_wb_ev) != 0) {
        unhook_vtable_slot(&g_hk_wb_d1);
        unhook_vtable_slot(&g_hk_wb_d0);
        unhook_vtable_slot(&g_hk_wb_ev);
        return;
    }
    g_vu7_base = wb_base;
    g_wb_hooked = 1;
    a7r3_log_hex("SDK_MTOUCH_WB_HOOK=", 1UL);
}

/* =========================================================================
 * Screenshot-Calibrated Hit Testing & Safe State-Guarded Input Dispatch
 *
 * Calibrated directly on A7R3 native 640x480 screenshots (old-menu-native.png):
 *  - Tab strip divider bottom: y=64; 7 tabs @ x=17,103,189,275,361,447,533 (w=91)
 *  - Title/page bar: y=65..124; left arrow area x=12..220, right/page area x=420..635
 *  - Main 6 rows: x=17..622, y=125..429 (50.8px pitch; row 1 box y=177..222)
 *  - Level-2 popup: outer [49, 21, 592, 456], inner rows x=97..544, y=91..396 (51px)
 *  - Custom-Key Function Assignment (state 6): side cols x<87 / x>=554, center 87..554
 *  - White Balance belt: x=0..114, y=51..386 (5 slots x 67px; center slot = 2)
 * ========================================================================= */

static int setting_view_valid(const uint8_t **out_vp)
{
    const void *v = g_cur_setting_view;
    if (!v || !a7r3_pm_readable(v, 0x148u))
        return 0;
    if (*(const unsigned long *)v != (g_vu2_base + VU2_VIEW_SETTING_VPTR))
        return 0;
    *out_vp = (const uint8_t *)v;
    return 1;
}

static int caution_active(void)
{
    const unsigned *p;
    if (!g_vu2_base)
        return 0;
    p = (const unsigned *)(g_vu2_base + VU2_CAUTION_WORD_OFF);
    if (!a7r3_pm_readable(p, 4u))
        return 0;
    return ((*p) & 0x4000u) != 0u;
}

static void drain_taps(void)
{
    int dx, dy;
    while (a7r3_input_poll_touch_tap(&dx, &dy)) {
    }
}

static int wait_int_eq(const volatile int *addr, int target, unsigned timeout_ms)
{
    unsigned start = a7r3_now_ms();
    while ((unsigned)(a7r3_now_ms() - start) < timeout_ms) {
        if (*addr == target)
            return 1;
        a7r3_nanosleep_ms(10u);
    }
    return (*addr == target);
}

static int handle_popup_tap(const uint8_t *vsm, const uint8_t *mlt, int x, int y)
{
    int vis_row, top, max_vis, target, cur, step;
    const volatile int *opt_ptr;

    /* Outside modal popup box -> cancel and return without applying selection */
    if (x < 49 || x >= 592 || y < 21 || y >= 456) {
        a7r3_input_send_key_click(A7R3_KEY_MENU, 20u);
        return 1;
    }

    /* Inside popup box but outside selectable rows (header/footer/padding) */
    if (x < 97 || x >= 544 || y < 91 || y >= 397)
        return 0;

    vis_row = (y - 91) / 51;
    if (vis_row < 0 || vis_row >= 6)
        return 0;

    max_vis = (int)*(const uint8_t *)(mlt + VU2_POPUP_MAX_VIS_OFF);
    if (max_vis <= 0 || max_vis > 6)
        max_vis = 6;
    if (vis_row >= max_vis)
        return 0;

    top = (int)*(const uint8_t *)(mlt + VU2_POPUP_TOP_OFF);
    target = top + vis_row;
    if (g_cur_popup_cnt > 0 && target >= g_cur_popup_cnt)
        return 0;

    opt_ptr = (const volatile int *)(vsm + 0x140u);
    cur = *opt_ptr;
    for (step = 0; step < 6 && cur != target; step++) {
        int next_expect;
        unsigned key;
        if (*(const volatile int *)(vsm + 0x04u) != 1 ||
            *(const volatile int *)(vsm + 0x130u) != 2 ||
            caution_active())
            return 0;
        key = (target > cur) ? A7R3_KEY_DOWN : A7R3_KEY_UP;
        next_expect = (target > cur) ? (cur + 1) : (cur - 1);
        a7r3_input_send_key_click(key, 18u);
        if (!wait_int_eq(opt_ptr, next_expect, 160u))
            break;
        cur = *opt_ptr;
    }

    if (*opt_ptr == target &&
        *(const volatile int *)(vsm + 0x04u) == 1 &&
        *(const volatile int *)(vsm + 0x130u) == 2 &&
        !caution_active()) {
        a7r3_input_send_key_click(A7R3_KEY_ENTER, 20u);
        return 1;
    }
    return 0;
}

static int handle_main_menu_tap(const uint8_t *vsm, const uint8_t *mlt, int x, int y)
{
    static const int16_t tab_x[7][2] = {
        {  17, 104 }, { 105, 190 }, { 191, 276 }, { 277, 362 },
        { 363, 448 }, { 449, 534 }, { 535, 624 },
    };

    /* 1. Top 7 category tabs (y = 0..64) */
    if (y >= 0 && y < 65) {
        int t;
        for (t = 0; t < 7; t++) {
            if (x >= tab_x[t][0] && x <= tab_x[t][1]) {
                int want_cat = t + 1;
                const volatile int *cat_ptr = (const volatile int *)(vsm + 0x134u);
                if (*cat_ptr != want_cat) {
                    g_req_mlt_cat = want_cat;
                    a7r3_dsb();
                    a7r3_input_send_key_click(A7R3_KEY_FN, 20u);
                    (void)wait_int_eq(cat_ptr, want_cat, 260u);
                    g_req_mlt_cat = 0;
                    a7r3_dsb();
                }
                return 1;
            }
        }
        return 0;
    }

    /* 2. Header subpage bar (y = 65..124): left & right zones */
    if (y >= 65 && y < 125) {
        if (x >= 12 && x <= 220) {
            a7r3_input_send_key_click(A7R3_KEY_LEFT, 20u);
            return 1;
        }
        if (x >= 420 && x <= 635) {
            a7r3_input_send_key_click(A7R3_KEY_RIGHT, 20u);
            return 1;
        }
        return 0;
    }

    /* 3. Main 6 list rows (x = 17..622, y = 125..429) */
    if (x >= 17 && x <= 622 && y >= 125 && y <= 429) {
        int row = ((y - 125) * 6) / 305;
        const volatile int *row_ptr = (const volatile int *)(vsm + 0x13cu);
        int max_rows = g_cur_mlt_row_cnt;
        (void)mlt;
        if (row < 0 || row >= 6)
            return 0;
        if (max_rows > 0 && row >= max_rows)
            return 0;
        if (*row_ptr != row) {
            g_req_mlt_row = row;
            a7r3_dsb();
            a7r3_input_send_key_click(A7R3_KEY_UP, 18u);
            if (!wait_int_eq(row_ptr, row, 260u)) {
                g_req_mlt_row = -1;
                a7r3_dsb();
                return 0;
            }
            g_req_mlt_row = -1;
            a7r3_dsb();
        }
        if (*(const volatile int *)(vsm + 0x04u) == 1 &&
            *(const volatile int *)(vsm + 0x130u) == 1 &&
            *row_ptr == row) {
            a7r3_input_send_key_click(A7R3_KEY_ENTER, 20u);
            return 1;
        }
    }
    return 0;
}

static int handle_custom_cat_tap(int x, int y)
{
    const uint8_t *cat = (const uint8_t *)g_cur_cat_wgt;
    int page_rows;
    const volatile int *row_ptr;

    if (!cat || !a7r3_pm_readable(cat, 0x1e0u) ||
        *(const unsigned long *)cat != (g_vu2_base + VU2_CAT_VPTR))
        return 0;

    if (y >= 65 && y < 125) {
        if (x >= 12 && x <= 220) {
            a7r3_input_send_key_click(A7R3_KEY_LEFT, 20u);
            return 1;
        }
        if (x >= 420 && x <= 635) {
            a7r3_input_send_key_click(A7R3_KEY_RIGHT, 20u);
            return 1;
        }
        return 0;
    }

    if (x >= 17 && x <= 622 && y >= 125 && y <= 429) {
        int row = ((y - 125) * 6) / 305;
        page_rows = *(const int *)(cat + 0x1c8u);
        row_ptr = (const volatile int *)(cat + 0x1d0u);
        if (row < 0 || row >= 6 || page_rows <= 0 || row >= page_rows)
            return 0;
        if (*row_ptr != row) {
            g_req_cat_row = row;
            a7r3_dsb();
            a7r3_input_send_key_click(A7R3_KEY_UP, 18u);
            if (!wait_int_eq(row_ptr, row, 260u)) {
                g_req_cat_row = -1;
                a7r3_dsb();
                return 0;
            }
            g_req_cat_row = -1;
            a7r3_dsb();
        }
        if (*row_ptr == row) {
            a7r3_input_send_key_click(A7R3_KEY_ENTER, 20u);
            return 1;
        }
    }
    return 0;
}

static int handle_custom_grp_tap(int x, int y)
{
    const uint8_t *grp = (const uint8_t *)g_cur_grp_wgt;
    int page_rows;
    const volatile int *row_ptr;

    if (!grp || !a7r3_pm_readable(grp, 0x1e0u) ||
        *(const unsigned long *)grp != (g_vu2_base + VU2_GRP_VPTR))
        return 0;

    if (y < 125 || y > 429)
        return 0;

    /* Dedicated full-height left/right arrow columns (calibrated x<87, x>=554) */
    if (x < 87) {
        a7r3_input_send_key_click(A7R3_KEY_LEFT, 20u);
        return 1;
    }
    if (x >= 554) {
        a7r3_input_send_key_click(A7R3_KEY_RIGHT, 20u);
        return 1;
    }

    /* Center option rows */
    {
        int row = ((y - 125) * 6) / 305;
        page_rows = *(const int *)(grp + 0x1c4u);
        row_ptr = (const volatile int *)(grp + 0x1ccu);
        if (row < 0 || row >= 6 || page_rows <= 0 || row >= page_rows)
            return 0;
        if (*row_ptr != row) {
            g_req_grp_row = row;
            a7r3_dsb();
            a7r3_input_send_key_click(A7R3_KEY_UP, 18u);
            if (!wait_int_eq(row_ptr, row, 260u)) {
                g_req_grp_row = -1;
                a7r3_dsb();
                return 0;
            }
            g_req_grp_row = -1;
            a7r3_dsb();
        }
        if (*row_ptr == row) {
            a7r3_input_send_key_click(A7R3_KEY_ENTER, 20u);
            return 1;
        }
    }
    return 0;
}

static int handle_wb_tap(int x, int y)
{
    const uint8_t *wb = (const uint8_t *)g_cur_wb_view;
    const uint8_t *belt;
    int list_num, raw_pos, cur_pos, slot, delta, target, step;

    if (!g_wb_hooked || !wb || !a7r3_pm_readable(wb, 0x1a0u))
        return 0;
    if (*(const unsigned long *)wb != (g_vu7_base + VU7_VIEW_WB_VPTR) ||
        *(const int *)(wb + 0x04u) != 1)
        return 0;

    belt = *(const uint8_t *const *)(wb + 0x194u);
    if (!belt || !a7r3_pm_readable(belt, 0x0b20u) ||
        *(const unsigned long *)belt != (g_vu2_base + VU2_BELT_VPTR))
        return 0;

    list_num = *(const int *)(belt + 0x0af8u);
    raw_pos  = *(const int *)(belt + 0x0b18u);
    if (list_num != 18 || raw_pos < 0 || raw_pos >= 72)
        return 0;
    cur_pos = raw_pos % 18;
    if (cur_pos < 0 || cur_pos >= 16)
        return 0;

    /* Left White Balance list belt: x=0..113, y=51..385 (5 slots x 67px) */
    if (x < 0 || x >= 114 || y < 51 || y >= 386)
        return 0;
    slot = (y - 51) / 67;
    if (slot < 0 || slot >= 5)
        return 0;
    delta = slot - 2;
    if (delta == 0) {
        a7r3_input_send_key_click(A7R3_KEY_ENTER, 20u);
        return 1;
    }

    target = cur_pos + delta;
    if (target < 0 || target >= 16)
        return 0;

    for (step = 0; step < 2 && cur_pos != target; step++) {
        unsigned key;
        int next_pos, start, cur2;
        if (*(const unsigned long *)wb != (g_vu7_base + VU7_VIEW_WB_VPTR) ||
            *(const int *)(wb + 0x04u) != 1)
            return 0;
        key = (target > cur_pos) ? A7R3_KEY_DOWN : A7R3_KEY_UP;
        next_pos = (target > cur_pos) ? (cur_pos + 1) : (cur_pos - 1);
        a7r3_input_send_key_click(key, 18u);
        start = (int)a7r3_now_ms();
        while ((int)(a7r3_now_ms() - (unsigned)start) < 160) {
            cur2 = (*(const int *)(belt + 0x0b18u)) % 18;
            if (cur2 == next_pos)
                break;
            a7r3_nanosleep_ms(10u);
        }
        cur_pos = (*(const int *)(belt + 0x0b18u)) % 18;
        if (cur_pos != next_pos)
            break;
    }
    return (cur_pos == target) ? 1 : 0;
}

static void process_one_tap(int x, int y)
{
    const uint8_t *vsm = NULL;
    int acted = 0;
    unsigned dbg = 0u;

    if (setting_view_valid(&vsm)) {
        int state = *(const int *)(vsm + 0x04u);
        int level = *(const int *)(vsm + 0x130u);
        dbg = (1u << 24) | (((unsigned)state & 0xffu) << 16) | (((unsigned)level & 0xffu) << 8);

        if (caution_active()) {
            dbg |= 0x80000000u;
            if (x >= 0 && x < 640 && y >= 0 && y < 480) {
                a7r3_input_send_key_click(A7R3_KEY_ENTER, 20u);
                acted = 1;
            }
        } else if (state == 1) {
            const uint8_t *mlt = (const uint8_t *)g_cur_mlt;
            if (mlt && a7r3_pm_readable(mlt, VU2_POPUP_OPEN_OFF + 8u) &&
                *(const unsigned long *)mlt == (g_vu2_base + VU2_MLT_VPTR)) {
                int popup_open = (*(const uint8_t *)(mlt + VU2_POPUP_OPEN_OFF) == 1u);
                if (popup_open)
                    dbg |= 0x40000000u;
                if (level == 2 && popup_open) {
                    acted = handle_popup_tap(vsm, mlt, x, y);
                } else if (level == 1 && !popup_open) {
                    acted = handle_main_menu_tap(vsm, mlt, x, y);
                }
            }
        } else if (state == 5) {
            acted = handle_custom_cat_tap(x, y);
        } else if (state == 6) {
            acted = handle_custom_grp_tap(x, y);
        }
    } else if (g_wb_hooked && g_cur_wb_view) {
        dbg = 0x02000000u;
        if (!caution_active())
            acted = handle_wb_tap(x, y);
    }

    dbg |= ((unsigned)acted & 0xffu);
    a7r3_log_hex("SDK_MTOUCH_TAP=", ((unsigned long)(unsigned)x << 16) | ((unsigned)y & 0xffffu));
    a7r3_log_hex("SDK_MTOUCH_RES=", (unsigned long)dbg);

    if (acted) {
        g_mt_actions++;
        drain_taps(); /* Never leak one tap into a newly opened layer */
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int a7r3_menu_touch_install(void)
{
    unsigned long vu2;

    if (g_mt_installed)
        return 0;

    vu2 = a7r3_mod_base("/viewUnified2.so");
    if (!vu2) {
        a7r3_log_str("SDK_MTOUCH_NO_VU2\n");
        return -1;
    }
    g_vu2_base = vu2;
    g_popup_get_cnt = (popup_cnt_fn)(vu2 + VU2_POPUP_GET_CNT_REAL);

    if (hook_vtable_slot(vu2, VU2_VIEW_SETTING_D1_SLOT, VU2_VIEW_SETTING_D1_REAL,
                         &g_hk_vsm_d1, (void *)&hook_vsm_d1, (void **)&g_real_vsm_d1) != 0 ||
        hook_vtable_slot(vu2, VU2_VIEW_SETTING_D0_SLOT, VU2_VIEW_SETTING_D0_REAL,
                         &g_hk_vsm_d0, (void *)&hook_vsm_d0, (void **)&g_real_vsm_d0) != 0 ||
        hook_vtable_slot(vu2, VU2_VIEW_SETTING_EV_SLOT, VU2_VIEW_SETTING_EV_REAL,
                         &g_hk_vsm_ev, (void *)&hook_vsm_ev, (void **)&g_real_vsm_ev) != 0 ||
        hook_vtable_slot(vu2, VU2_MLT_NAV_SLOT, VU2_MLT_NAV_REAL,
                         &g_hk_mlt_nav, (void *)&hook_mlt_nav, (void **)&g_real_mlt_nav) != 0 ||
        hook_vtable_slot(vu2, VU2_MLT_CAT_SLOT, VU2_MLT_CAT_REAL,
                         &g_hk_mlt_cat, (void *)&hook_mlt_cat, (void **)&g_real_mlt_cat) != 0 ||
        hook_vtable_slot(vu2, VU2_CAT_UPD_SLOT, VU2_CAT_UPD_REAL,
                         &g_hk_cat_upd, (void *)&hook_cat_upd, (void **)&g_real_cat_upd) != 0 ||
        hook_vtable_slot(vu2, VU2_CAT_NAV_SLOT, VU2_CAT_NAV_REAL,
                         &g_hk_cat_nav, (void *)&hook_cat_nav, (void **)&g_real_cat_nav) != 0 ||
        hook_vtable_slot(vu2, VU2_GRP_UPD_SLOT, VU2_GRP_UPD_REAL,
                         &g_hk_grp_upd, (void *)&hook_grp_upd, (void **)&g_real_grp_upd) != 0 ||
        hook_vtable_slot(vu2, VU2_GRP_NAV_SLOT, VU2_GRP_NAV_REAL,
                         &g_hk_grp_nav, (void *)&hook_grp_nav, (void **)&g_real_grp_nav) != 0) {
        a7r3_log_str("SDK_MTOUCH_HOOK_MISMATCH\n");
        a7r3_menu_touch_remove();
        return -2;
    }

    try_hook_wb();
    g_mt_installed = 1;
    a7r3_log_hex("SDK_MTOUCH_HOOKS=", 1UL);
    return 0;
}

void a7r3_menu_touch_remove(void)
{
    a7r3_menu_touch_set_enabled(0);

    unhook_vtable_slot(&g_hk_wb_ev);
    unhook_vtable_slot(&g_hk_wb_d0);
    unhook_vtable_slot(&g_hk_wb_d1);
    g_wb_hooked = 0;

    unhook_vtable_slot(&g_hk_grp_nav);
    unhook_vtable_slot(&g_hk_grp_upd);
    unhook_vtable_slot(&g_hk_cat_nav);
    unhook_vtable_slot(&g_hk_cat_upd);
    unhook_vtable_slot(&g_hk_mlt_cat);
    unhook_vtable_slot(&g_hk_mlt_nav);
    unhook_vtable_slot(&g_hk_vsm_ev);
    unhook_vtable_slot(&g_hk_vsm_d0);
    unhook_vtable_slot(&g_hk_vsm_d1);

    g_cur_setting_view = NULL;
    g_cur_mlt = NULL;
    g_cur_cat_wgt = NULL;
    g_cur_grp_wgt = NULL;
    g_cur_wb_view = NULL;
    a7r3_dsb();
    a7r3_nanosleep_ms(30u);

    if (g_mt_installed) {
        a7r3_log_hex("SDK_MTOUCH_RESTORED=", 1UL);
        g_mt_installed = 0;
    }
}

void a7r3_menu_touch_set_enabled(int enable)
{
    int on = enable ? 1 : 0;
    if (on == g_menu_touch_enabled)
        return;
    g_menu_touch_enabled = on;
    a7r3_input_set_touch_tap_watch(on);
    drain_taps();
    a7r3_dsb();
    a7r3_log_hex("SDK_MTOUCH_EN=", (unsigned long)on);
}

int a7r3_menu_touch_get_enabled(void)
{
    return g_menu_touch_enabled;
}

void a7r3_menu_touch_notify_mlt(void *mlt, int cur_cat, int cur_page, int cur_row_cnt)
{
    (void)cur_cat;
    (void)cur_page;
    g_cur_mlt = mlt;
    g_cur_mlt_row_cnt = cur_row_cnt;
    a7r3_dsb();
}

void a7r3_menu_touch_tick(void)
{
    static unsigned last_wb_probe_ms;
    int x = 0, y = 0;
    unsigned now;

    if (!g_mt_installed || !g_menu_touch_enabled)
        return;

    now = a7r3_now_ms();
    if (!g_wb_hooked && (unsigned)(now - last_wb_probe_ms) >= 500u) {
        last_wb_probe_ms = now;
        try_hook_wb();
    }

    while (a7r3_input_poll_touch_tap(&x, &y)) {
        int dx = x - g_last_tap_x;
        int dy = y - g_last_tap_y;
        now = a7r3_now_ms();
        g_mt_taps++;
        if (dx >= -4 && dx <= 4 && dy >= -4 && dy <= 4 &&
            (unsigned)(now - g_last_tap_ms) < 350u) {
            continue;
        }
        g_last_tap_x = x;
        g_last_tap_y = y;
        g_last_tap_ms = now;
        process_one_tap(x, y);
        break;
    }
}

int a7r3_menu_touch_take_stats(unsigned *out_taps, unsigned *out_actions)
{
    unsigned t = g_mt_taps;
    unsigned a = g_mt_actions;
    if (t == g_last_mt_taps && a == g_last_mt_actions)
        return 0;
    g_last_mt_taps = t;
    g_last_mt_actions = a;
    if (out_taps)
        *out_taps = t;
    if (out_actions)
        *out_actions = a;
    return 1;
}
