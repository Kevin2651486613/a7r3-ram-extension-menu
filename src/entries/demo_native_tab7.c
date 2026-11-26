#include "a7r3_sys.h"
#include "a7r3_fb.h"
#include "a7r3_input.h"
#include "a7r3_dual_hud.h"
#include "a7r3_pb_focus.h"
#include "a7r3_shutter_sfx.h"
#include "a7r3_menu_touch.h"
#include "a7r3_native_tab7.h"

#define GATE_ARM  "/tmp_bt/addon_arm"
#define GATE_OFF  "/tmp_bt/addon_off"
#define GATE_QUIT "/tmp_bt/addon_quit"

extern int pthread_create(unsigned long *tid, const void *attr,
                          void *(*fn)(void *), void *arg)
    __attribute__((weak));

static void *native_tab7_worker(void *arg)
{
    unsigned last_gate_ms = 0u;
    unsigned last_sfx_ms = 0u;

    (void)arg;

    a7r3_log_init("/tmp_bt/addon.log");
    a7r3_log_str("SDK_TAB7_ALIVE\n");
    a7r3_maps_reload();

    if (a7r3_fb_open() != 0) {
        a7r3_log_str("SDK_FB_FAIL\n");
        return NULL;
    }

    /* Native Route A1 never draws a full-screen overlay; keep guard=0 & swallow=0
     * so Sony's GPU UI runs at full native frame rate with zero flicker overhead. */
    a7r3_fb_install_ioctl_hook();
    a7r3_fb_set_guard_active(0);

    a7r3_input_install();
    a7r3_input_set_swallow(0, 0);

    a7r3_dual_hud_install();
    a7r3_pb_focus_install();
    a7r3_shutter_sfx_install();
    a7r3_shutter_sfx_refresh_state();
    a7r3_menu_touch_install();

    if (a7r3_native_tab7_install() != 0)
        a7r3_log_str("SDK_TAB7_INSTALL_ERR\n");

    for (;;) {
        unsigned now;
        unsigned mh = 0, gh = 0, th = 0;
        unsigned dp = 0, dg = 0, dr = 0;
        unsigned mt_taps = 0, mt_acts = 0;

        a7r3_nanosleep_ms(a7r3_menu_touch_get_enabled() ? 15u : 25u);
        now = a7r3_now_ms();

        if ((unsigned)(now - last_gate_ms) >= 200u) {
            last_gate_ms = now;
            if (a7r3_file_exists(GATE_QUIT)) {
                a7r3_input_set_ev_dial_shield(0);
                a7r3_dual_hud_set_enabled(0);
                a7r3_pb_focus_set_enabled(0);
                a7r3_shutter_sfx_set_enabled(0);
                a7r3_menu_touch_set_enabled(0);

                a7r3_native_tab7_remove();
                a7r3_menu_touch_remove();
                a7r3_shutter_sfx_uninstall();
                a7r3_pb_focus_remove();
                a7r3_dual_hud_remove();
                a7r3_input_remove();
                a7r3_fb_remove_ioctl_hook();
                a7r3_fb_close();
                a7r3_log_str("SDK_TAB7_QUIT\n");
                return NULL;
            }
        }

        if ((unsigned)(now - last_sfx_ms) >= 500u) {
            last_sfx_ms = now;
            a7r3_shutter_sfx_refresh_state();
        }

        a7r3_menu_touch_tick();
        a7r3_pb_focus_tick(0);

        if (a7r3_menu_touch_take_stats(&mt_taps, &mt_acts)) {
            a7r3_log_hex("SDK_MTOUCH_TAPS=", (unsigned long)mt_taps);
            a7r3_log_hex("SDK_MTOUCH_ACTS=", (unsigned long)mt_acts);
        }

        if (a7r3_native_tab7_take_stats(&mh, &gh, &th)) {
            a7r3_log_hex("SDK_TAB7_MLT_HITS=", (unsigned long)mh);
            a7r3_log_hex("SDK_TAB7_MGR_HITS=", (unsigned long)gh);
            a7r3_log_hex("SDK_TAB7_TXT_HITS=", (unsigned long)th);
        }
        if (a7r3_dual_hud_take_stats(&dp, &dg, &dr)) {
            a7r3_log_hex("SDK_DUAL_PARSE=", (unsigned long)dp);
            a7r3_log_hex("SDK_DUAL_GATE1=", (unsigned long)dg);
            a7r3_log_hex("SDK_DUAL_ROUTE=", (unsigned long)dr);
        }
    }
}

__attribute__((constructor)) static void demo_tab7_ctor(void)
{
    unsigned long tid = 0;
    int (*volatile pcreate)(unsigned long *, const void *,
                            void *(*)(void *), void *) = pthread_create;
    if (!a7r3_comm_is_im())
        return;
    if (!a7r3_file_exists(GATE_ARM) || a7r3_file_exists(GATE_OFF))
        return;
    if (pcreate)
        pcreate(&tid, NULL, native_tab7_worker, NULL);
}
