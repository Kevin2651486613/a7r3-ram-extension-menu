#include "a7r3_input.h"
#include "a7r3_fb.h"
#include "a7r3_sys.h"

#define LIBOBJ_OSAL_RCV_GOT    0x0121c8c0UL
#define LIBOBJ_OSAL_FREE_GOT   0x0121c7a8UL
#define LIBOBJ_OSAL_VALLOC_GOT 0x0121c35cUL
#define LIBOBJ_OSAL_SND_GOT    0x0121ca94UL
#define LIBOBJ_GET_PHY_GOT     0x0121c990UL
#define LIBOBJ_GET_EV_GOT      0x0121ac2cUL
#define LIBOBJ_GET_PHY_REAL    0x0084b701UL /* Thumb */
#define LIBOBJ_GET_EV_REAL     0x0084c765UL /* Thumb */
#define LIBOBJ_INFRATP_PTR     0x012d4ebcUL
#define LIBOBJ_INFRAINPUT_PTR  0x012d4ee8UL
#define QID_INPUT_MAIN         0x007a0020u
#define QID_TP_MAIN            0x007b0026u
#define EV_SYNTH_TAG           0x5A3E0000u
#define EVQ_SZ                 16u

typedef int (*osal_rcv_fn)(unsigned qid, void **msg_out, int timeout);
typedef int (*osal_free_fn)(unsigned qid, void *msg);
typedef int (*osal_valloc_fn)(unsigned qid, void **msg_out, unsigned size, int timeout);
typedef int (*osal_snd_fn)(unsigned qid, void *msg);
typedef int (*get_phy_fn)(void *self, int key_code, void *out_data);
typedef int (*get_ev_fn)(void *self, int handle, int key_code, void *out_ev);

struct raw_ev {
    unsigned w0, w1, w2, w3;
};

struct tap_ev {
    int x, y;
};

static struct raw_ev g_key_ring[EVQ_SZ];
static volatile unsigned g_key_head, g_key_tail;
static struct raw_ev g_tp_ring[EVQ_SZ];
static volatile unsigned g_tp_head, g_tp_tail;
static struct tap_ev g_tap_ring[EVQ_SZ];
static volatile unsigned g_tap_head, g_tap_tail;
static volatile int g_tp_tap_watch;
static int g_tap_down;
static unsigned g_tap_last_w1;

static void calibrate_touch(unsigned packed_xy, int *ox, int *oy);

static volatile unsigned long *g_osal_got;
static osal_rcv_fn g_orig_osal_rcv;
static osal_free_fn g_osal_free_msg;
static osal_valloc_fn g_osal_valloc;
static osal_snd_fn g_osal_snd_msg;
static volatile unsigned long *g_get_phy_got;
static unsigned long g_orig_get_phy_got;
static get_phy_fn g_real_get_phy;
static volatile unsigned long *g_get_ev_got;
static unsigned long g_orig_get_ev_got;
static get_ev_fn g_real_get_ev;
static volatile int g_hook_active;
static volatile int g_swallow_keys;
static volatile int g_swallow_touch;
static volatile int g_ev_dial_shield;
static volatile unsigned g_last_input_qid = QID_INPUT_MAIN;
static volatile unsigned g_ev_real_code;
static volatile unsigned g_ev_real_w1;
static volatile unsigned g_ev_real_w2;
static volatile int g_ev_real_valid;
static volatile int g_ev_log_pending;
static volatile a7r3_ext_dismiss_cb g_ext_cb;
static volatile unsigned g_ext_code;
static volatile unsigned g_ext_status;
static volatile int g_ext_pending;
static volatile unsigned g_dsw_code;
static volatile unsigned g_dsw_status;
static volatile int g_dsw_pending;
static unsigned long g_obj_base;
static int g_tp_down;
static volatile unsigned g_native_act_seq;

static int is_ev_dial_code(unsigned code)
{
    return code == A7R3_KEY_EV_DIAL1 || code == A7R3_KEY_EV_DIAL2;
}

static int is_passive_sensor_event(unsigned code)
{
    return code == 0x18000001u || /* KEY_DIGITALLEVEL_ANGLE */
           code == 0x17000001u || code == 0x17000002u || code == 0x17000003u || /* KEY_ILLUMI_LEVEL_* */
           code == 0x06000001u || code == 0x06000002u || code == 0x06000081u;   /* KEY_TP_* */
}

static int is_nav_key(unsigned code)
{
    return code == A7R3_KEY_UP || code == A7R3_KEY_DOWN ||
           code == A7R3_KEY_LEFT || code == A7R3_KEY_RIGHT ||
           code == A7R3_KEY_ENTER || code == A7R3_KEY_MENU;
}

static int is_display_switch_event(unsigned code)
{
    return code == A7R3_KEY_EYE_CONTACT || code == A7R3_KEY_FINDER_LCD ||
           code == A7R3_KEY_EVF_OPEN    || code == A7R3_KEY_EVF_CLOSE ||
           code == A7R3_KEY_PANEL_TILT  ||
           code == 0x70a6u || code == 0x70a7u || code == 0x70a8u || code == 0x70d1u;
}

static int nm_osal_rcv_hook(unsigned qid, void **msg_out, int timeout)
{
    unsigned q = qid & 0x00ffffffu;

    if (!g_hook_active || (q != QID_INPUT_MAIN && q != QID_TP_MAIN))
        return g_orig_osal_rcv(qid, msg_out, timeout);

    for (;;) {
        int rc = g_orig_osal_rcv(qid, msg_out, timeout);
        void *msg;
        const unsigned *w;

        if (!g_hook_active || rc != 0 || !msg_out || !*msg_out)
            return rc;
        msg = *msg_out;
        w = (const unsigned *)msg;
        if (q == QID_INPUT_MAIN) {
            g_last_input_qid = qid;
            if (is_ev_dial_code(w[0])) {
                if (w[3] == EV_SYNTH_TAG) {
                    ((unsigned *)msg)[3] = 0u;
                    a7r3_dsb();
                    return rc;
                }
                g_ev_real_code = w[0];
                g_ev_real_w1 = w[1];
                g_ev_real_w2 = w[2];
                g_ev_real_valid = 1;
                g_ev_log_pending = 1;
                a7r3_dsb();
                if (g_ev_dial_shield) {
                    if (g_osal_free_msg) {
                        g_osal_free_msg(qid, msg);
                        continue;
                    }
                    ((unsigned *)msg)[1] = 0u;
                    ((unsigned *)msg)[2] = 0u;
                    a7r3_dsb();
                    return rc;
                }
            }
            if (!is_nav_key(w[0])) {
                if (is_display_switch_event(w[0])) {
                    if (g_swallow_keys) {
                        g_dsw_code = w[0];
                        g_dsw_status = w[2];
                        g_dsw_pending = 1;
                        a7r3_dsb();
                    }
                    return rc;
                }
                if (is_passive_sensor_event(w[0]))
                    return rc;
                if (g_swallow_keys) {
                    a7r3_ext_dismiss_cb cb = g_ext_cb;
                    g_swallow_keys = 0;
                    g_swallow_touch = 0;
                    g_ext_code = w[0];
                    g_ext_status = w[2];
                    g_ext_pending = 1;
                    a7r3_dsb();
                    if (cb)
                        cb(w[0], w[2]);
                } else {
                    g_native_act_seq++;
                }
                return rc;
            }
            if (g_swallow_keys) {
                unsigned h = g_key_head;
                if ((h - g_key_tail) < EVQ_SZ) {
                    struct raw_ev *e = &g_key_ring[h & (EVQ_SZ - 1u)];
                    e->w0 = w[0];
                    e->w1 = w[1];
                    e->w2 = w[2];
                    e->w3 = w[3];
                    a7r3_dsb();
                    g_key_head = h + 1u;
                }
                if (g_osal_free_msg) {
                    g_osal_free_msg(qid, msg);
                    continue;
                }
            }
            g_native_act_seq++;
            return rc;
        } else {
            if (g_swallow_touch) {
                unsigned h = g_tp_head;
                if ((h - g_tp_tail) < EVQ_SZ) {
                    struct raw_ev *e = &g_tp_ring[h & (EVQ_SZ - 1u)];
                    e->w0 = w[0];
                    e->w1 = w[1];
                    e->w2 = w[2];
                    e->w3 = w[3];
                    a7r3_dsb();
                    g_tp_head = h + 1u;
                }
                if (g_osal_free_msg) {
                    g_osal_free_msg(qid, msg);
                    continue;
                }
            }
            if (g_tp_tap_watch) {
                if ((w[2] == 1u || w[2] == 2u) && w[1] != 0xffffffffu) {
                    g_tap_last_w1 = w[1];
                    g_tap_down = 1;
                } else if (w[2] == 0u || w[2] == 3u || w[1] == 0xffffffffu) {
                    if (g_tap_down) {
                        unsigned raw = (w[2] == 0u && w[1] != 0xffffffffu && w[1] != 0u)
                                           ? w[1]
                                           : g_tap_last_w1;
                        int tx = 0, ty = 0;
                        unsigned h = g_tap_head;
                        calibrate_touch(raw, &tx, &ty);
                        if ((h - g_tap_tail) < EVQ_SZ) {
                            struct tap_ev *e = &g_tap_ring[h & (EVQ_SZ - 1u)];
                            e->x = tx;
                            e->y = ty;
                            a7r3_dsb();
                            g_tap_head = h + 1u;
                        }
                        g_tap_down = 0;
                    }
                }
            }
            g_native_act_seq++;
            return rc;
        }
    }
}

static int ev_get_phy_hook(void *self, int key_code, void *out_data)
{
    int rc = g_real_get_phy ? g_real_get_phy(self, key_code, out_data) : 0;
    if (g_ev_dial_shield && is_ev_dial_code((unsigned)key_code)) {
        if (out_data) {
            unsigned *p = (unsigned *)out_data;
            p[0] = (unsigned)key_code;
            p[1] = 0u;
            p[2] = 0u;
        }
        return 0;
    }
    return rc;
}

static int ev_get_ev_hook(void *self, int handle, int key_code, void *out_ev)
{
    int rc = g_real_get_ev ? g_real_get_ev(self, handle, key_code, out_ev) : 0;
    if (g_ev_dial_shield && is_ev_dial_code((unsigned)key_code)) {
        if (out_ev) {
            unsigned *p = (unsigned *)out_ev;
            p[1] = 0u;
            p[2] = 0u;
        }
        return 0;
    }
    return rc;
}

static void *get_infra_input_inst(void)
{
    const void *const *pp;
    if (!g_obj_base)
        return NULL;
    pp = (const void *const *)(g_obj_base + LIBOBJ_INFRAINPUT_PTR);
    if (!a7r3_pm_readable(pp, 4u) || !a7r3_pm_readable(*pp, 0x38u))
        return NULL;
    return (void *)(uintptr_t)*pp;
}

static void capture_ev_phy_cache(unsigned *out_nonzero_code,
                                 unsigned *out_w1,
                                 unsigned *out_w2)
{
    const unsigned char *inst = (const unsigned char *)get_infra_input_inst();
    unsigned key_cnt, i;
    unsigned long base;

    if (out_nonzero_code)
        *out_nonzero_code = 0u;
    if (out_w1)
        *out_w1 = 0u;
    if (out_w2)
        *out_w2 = 0u;
    if (!inst)
        return;
    key_cnt = *(const unsigned *)(inst + 0x1c);
    base = *(const unsigned long *)(inst + 0x24);
    if (base <= 0x10000UL || key_cnt == 0u)
        return;
    if (key_cnt > 1024u)
        key_cnt = 1024u;
    for (i = 0u; i < key_cnt; i++) {
        const unsigned *p = (const unsigned *)(base + (unsigned long)i * 0x28UL);
        if (!a7r3_pm_readable((const void *)p, 16u))
            break;
        if (is_ev_dial_code(p[0]) && (p[1] != 0u || p[2] != 0u)) {
            g_ev_real_code = p[0];
            g_ev_real_w1 = p[1];
            g_ev_real_w2 = p[2];
            g_ev_real_valid = 1;
            a7r3_dsb();
            if (out_nonzero_code && *out_nonzero_code == 0u) {
                *out_nonzero_code = p[0];
                if (out_w1)
                    *out_w1 = p[1];
                if (out_w2)
                    *out_w2 = p[2];
            }
        }
    }
}

static int send_input_msg4(unsigned w0, unsigned w1, unsigned w2, unsigned w3)
{
    unsigned qid = g_last_input_qid;
    void *msg = NULL;
    unsigned *w;

    if (!g_osal_valloc || !g_osal_snd_msg)
        return -1;
    if (g_osal_valloc(qid, &msg, 16u, 100) != 0 || !msg)
        return -1;
    w = (unsigned *)msg;
    w[0] = w0;
    w[1] = w1;
    w[2] = w2;
    w[3] = w3;
    a7r3_dsb();
    if (g_osal_snd_msg(qid, msg) != 0) {
        if (g_osal_free_msg)
            g_osal_free_msg(qid, msg);
        return -1;
    }
    return 0;
}

static void send_ev_synth(unsigned key_code, unsigned w1, unsigned w2)
{
    (void)send_input_msg4(key_code, w1, w2, EV_SYNTH_TAG);
}

int a7r3_input_install(void)
{
    unsigned long obj_base, uipc_lo, orig_rcv, orig_free;
    unsigned long valloc_fn_va, snd_fn_va;
    volatile unsigned long *rcv_slot, *free_slot, *valloc_slot, *snd_slot;
    volatile unsigned long *phy_slot, *ev_slot;

    if (g_osal_got)
        return 0;
    obj_base = a7r3_mod_base("/libObj.so");
    uipc_lo = a7r3_mod_base("/libosal_uipc.so");
    if (!obj_base || !uipc_lo)
        return -1;
    g_obj_base = obj_base;
    free_slot = (volatile unsigned long *)(obj_base + LIBOBJ_OSAL_FREE_GOT);
    rcv_slot = (volatile unsigned long *)(obj_base + LIBOBJ_OSAL_RCV_GOT);
    if (!a7r3_pm_readable((const void *)free_slot, 4) ||
        !a7r3_pm_writable((const void *)rcv_slot, 4))
        return -2;
    orig_rcv = *rcv_slot;
    orig_free = *free_slot;
    if (orig_rcv < uipc_lo || orig_rcv > uipc_lo + 0x100000UL ||
        orig_free < uipc_lo || orig_free > uipc_lo + 0x100000UL)
        return -3;

    valloc_slot = (volatile unsigned long *)(obj_base + LIBOBJ_OSAL_VALLOC_GOT);
    snd_slot = (volatile unsigned long *)(obj_base + LIBOBJ_OSAL_SND_GOT);
    if (a7r3_pm_readable((const void *)valloc_slot, 4) &&
        a7r3_pm_readable((const void *)snd_slot, 4)) {
        valloc_fn_va = *valloc_slot;
        snd_fn_va = *snd_slot;
        if (valloc_fn_va >= uipc_lo && valloc_fn_va <= uipc_lo + 0x100000UL &&
            snd_fn_va >= uipc_lo && snd_fn_va <= uipc_lo + 0x100000UL) {
            g_osal_valloc = (osal_valloc_fn)valloc_fn_va;
            g_osal_snd_msg = (osal_snd_fn)snd_fn_va;
        }
    }

    phy_slot = (volatile unsigned long *)(obj_base + LIBOBJ_GET_PHY_GOT);
    ev_slot = (volatile unsigned long *)(obj_base + LIBOBJ_GET_EV_GOT);
    if (a7r3_pm_writable((const void *)phy_slot, 4) &&
        a7r3_pm_writable((const void *)ev_slot, 4)) {
        g_orig_get_phy_got = *phy_slot;
        g_orig_get_ev_got = *ev_slot;
        g_real_get_phy = (get_phy_fn)(obj_base + LIBOBJ_GET_PHY_REAL);
        g_real_get_ev = (get_ev_fn)(obj_base + LIBOBJ_GET_EV_REAL);
        a7r3_dsb();
        *phy_slot = (unsigned long)(uintptr_t)&ev_get_phy_hook;
        *ev_slot = (unsigned long)(uintptr_t)&ev_get_ev_hook;
        a7r3_dsb();
        g_get_phy_got = phy_slot;
        g_get_ev_got = ev_slot;
        a7r3_log_hex("SDK_EV_HOOKS=", 1UL);
    }

    g_orig_osal_rcv = (osal_rcv_fn)orig_rcv;
    g_osal_free_msg = (osal_free_fn)orig_free;
    g_hook_active = 1;
    a7r3_dsb();
    *rcv_slot = (unsigned long)(uintptr_t)&nm_osal_rcv_hook;
    a7r3_dsb();
    g_osal_got = rcv_slot;
    a7r3_log_hex("SDK_OSAL_HOOK=", (unsigned long)rcv_slot);
    a7r3_log_hex("SDK_OSAL_ORIG=", orig_rcv);
    return 0;
}

void a7r3_input_remove(void)
{
    g_hook_active = 0;
    g_swallow_keys = 0;
    g_swallow_touch = 0;
    g_tp_tap_watch = 0;
    g_tap_down = 0;
    g_ev_dial_shield = 0;
    a7r3_dsb();
    if (g_get_phy_got) {
        *g_get_phy_got = g_orig_get_phy_got;
        g_get_phy_got = NULL;
    }
    if (g_get_ev_got) {
        *g_get_ev_got = g_orig_get_ev_got;
        g_get_ev_got = NULL;
    }
    a7r3_dsb();
    if (g_osal_got && g_orig_osal_rcv) {
        *g_osal_got = (unsigned long)(uintptr_t)g_orig_osal_rcv;
        a7r3_dsb();
        a7r3_nanosleep_ms(50);
        a7r3_log_hex("SDK_OSAL_RESTORED=", (unsigned long)g_osal_got);
        g_osal_got = NULL;
    }
}

void a7r3_input_set_ev_dial_shield(int enable)
{
    int on = enable ? 1 : 0;
    if (on == g_ev_dial_shield)
        return;
    if (on) {
        unsigned nz_code = 0u, nz_w1 = 0u, nz_w2 = 0u;
        capture_ev_phy_cache(&nz_code, &nz_w1, &nz_w2);
        g_ev_dial_shield = 1;
        a7r3_dsb();
        if (nz_code != 0u)
            send_ev_synth(nz_code, 0u, 0u);
        a7r3_log_hex("SDK_EV_SHIELD=", 1UL);
        a7r3_log_hex("SDK_EV_NZ_CODE=", (unsigned long)nz_code);
        a7r3_log_hex("SDK_EV_NZ_W1=", (unsigned long)nz_w1);
        a7r3_log_hex("SDK_EV_NZ_W2=", (unsigned long)nz_w2);
    } else {
        g_ev_dial_shield = 0;
        a7r3_dsb();
        if (g_ev_real_valid && g_ev_real_code != 0u &&
            (g_ev_real_w1 != 0u || g_ev_real_w2 != 0u)) {
            unsigned c = g_ev_real_code;
            unsigned w1 = g_ev_real_w1;
            unsigned w2 = g_ev_real_w2;
            send_ev_synth(c, w1, w2);
            a7r3_log_hex("SDK_EV_RESYNC_CODE=", (unsigned long)c);
            a7r3_log_hex("SDK_EV_RESYNC_W1=", (unsigned long)w1);
            a7r3_log_hex("SDK_EV_RESYNC_W2=", (unsigned long)w2);
        }
        a7r3_log_hex("SDK_EV_SHIELD=", 0UL);
    }
}

int a7r3_input_get_ev_dial_shield(void)
{
    return g_ev_dial_shield;
}

int a7r3_input_take_ev_raw(unsigned *out_code, unsigned *out_w1, unsigned *out_w2)
{
    if (!g_ev_log_pending)
        return 0;
    if (out_code)
        *out_code = g_ev_real_code;
    if (out_w1)
        *out_w1 = g_ev_real_w1;
    if (out_w2)
        *out_w2 = g_ev_real_w2;
    g_ev_log_pending = 0;
    a7r3_dsb();
    return 1;
}

void a7r3_input_set_swallow(int swallow_keys, int swallow_touch)
{
    g_swallow_keys = swallow_keys ? 1 : 0;
    g_swallow_touch = swallow_touch ? 1 : 0;
    a7r3_dsb();
}

void a7r3_input_flush(void)
{
    g_key_tail = g_key_head;
    g_tp_tail = g_tp_head;
    g_tp_down = 0;
    a7r3_dsb();
}

void a7r3_input_set_ext_dismiss_cb(a7r3_ext_dismiss_cb cb)
{
    g_ext_cb = cb;
    a7r3_dsb();
}

int a7r3_input_take_ext_dismiss(unsigned *out_code, unsigned *out_status)
{
    if (!g_ext_pending)
        return 0;
    if (out_code)
        *out_code = g_ext_code;
    if (out_status)
        *out_status = g_ext_status;
    g_ext_pending = 0;
    a7r3_dsb();
    return 1;
}

int a7r3_input_take_disp_switch(unsigned *out_code, unsigned *out_status)
{
    if (!g_dsw_pending)
        return 0;
    if (out_code)
        *out_code = g_dsw_code;
    if (out_status)
        *out_status = g_dsw_status;
    g_dsw_pending = 0;
    a7r3_dsb();
    return 1;
}

int a7r3_input_poll_key(struct a7r3_key_ev *out)
{
    struct raw_ev e;
    if (!out || g_key_tail == g_key_head)
        return 0;
    e = g_key_ring[g_key_tail & (EVQ_SZ - 1u)];
    a7r3_dsb();
    g_key_tail++;
    out->code = e.w0;
    out->status = e.w2;
    return 1;
}

static void calibrate_touch(unsigned packed_xy, int *ox, int *oy)
{
    int no_fx = 1, no_fy = 1, sw = (int)A7R3_FB_W, sh = (int)A7R3_FB_H;
    int dx0 = 0, dy0 = 0, rx0 = 0, ry0 = 0, sx = 642, sy = 481;
    int raw_x = (int)(packed_xy >> 16);
    int raw_y = (int)(packed_xy & 0xffffu);
    const char *tp = NULL;
    int x, y;

    if (g_obj_base) {
        const void *const *pp = (const void *const *)(g_obj_base + LIBOBJ_INFRATP_PTR);
        if (a7r3_pm_readable(pp, 4) && a7r3_pm_readable(*pp, 0x88))
            tp = (const char *)*pp;
    }
    if (tp) {
        int cw = *(const int *)(tp + 0x58);
        int ch = *(const int *)(tp + 0x5c);
        if (cw > 0 && ch > 0) {
            no_fx = *(const unsigned char *)(tp + 0x30);
            no_fy = *(const unsigned char *)(tp + 0x31);
            sw = cw;
            sh = ch;
            dx0 = *(const int *)(tp + 0x60);
            dy0 = *(const int *)(tp + 0x64);
            rx0 = *(const int *)(tp + 0x68);
            ry0 = *(const int *)(tp + 0x6c);
            sx = *(const int *)(tp + 0x80);
            sy = *(const int *)(tp + 0x84);
        }
    }
    x = dx0 + (((raw_x - rx0) * sx) >> 8);
    y = dy0 + (((raw_y - ry0) * sy) >> 8);
    if (!no_fx) x = sw - x;
    if (!no_fy) y = sh - y;
    if (x < 0) x = 0; else if (x >= sw) x = sw - 1;
    if (y < 0) y = 0; else if (y >= sh) y = sh - 1;
    *ox = x;
    *oy = y;
}

int a7r3_input_poll_touch(struct a7r3_touch_ev *out)
{
    while (g_tp_tail != g_tp_head) {
        struct raw_ev e = g_tp_ring[g_tp_tail & (EVQ_SZ - 1u)];
        int x, y;
        a7r3_dsb();
        g_tp_tail++;
        if (e.w2 == 0u || e.w2 == 3u || e.w1 == 0xffffffffu) {
            int was_down = g_tp_down;
            g_tp_down = 0;
            if (was_down && out) {
                out->x = 0;
                out->y = 0;
                out->down = 0;
                return 1;
            }
            continue;
        }
        if (e.w2 != 1u || g_tp_down)
            continue;
        g_tp_down = 1;
        calibrate_touch(e.w1, &x, &y);
        if (out) {
            out->x = x;
            out->y = y;
            out->down = 1;
            return 1;
        }
    }
    return 0;
}

unsigned a7r3_input_native_activity_seq(void)
{
    return g_native_act_seq;
}

int a7r3_input_send_key_click(unsigned key_code, unsigned hold_ms)
{
    if (hold_ms == 0u)
        hold_ms = 20u;
    if (send_input_msg4(key_code, 0u, 1u, 0u) != 0)
        return -1;
    a7r3_nanosleep_ms(hold_ms);
    if (send_input_msg4(key_code, 0u, 0u, 0u) != 0)
        return -1;
    return 0;
}

void a7r3_input_set_touch_tap_watch(int enable)
{
    int on = enable ? 1 : 0;
    if (on == g_tp_tap_watch)
        return;
    g_tp_tap_watch = 0;
    a7r3_dsb();
    g_tap_tail = g_tap_head;
    g_tap_down = 0;
    g_tap_last_w1 = 0u;
    a7r3_dsb();
    g_tp_tap_watch = on;
    a7r3_dsb();
}

int a7r3_input_poll_touch_tap(int *out_x, int *out_y)
{
    struct tap_ev e;
    if (g_tap_tail == g_tap_head)
        return 0;
    e = g_tap_ring[g_tap_tail & (EVQ_SZ - 1u)];
    a7r3_dsb();
    g_tap_tail++;
    if (out_x)
        *out_x = e.x;
    if (out_y)
        *out_y = e.y;
    return 1;
}
