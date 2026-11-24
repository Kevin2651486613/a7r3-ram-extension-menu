#ifndef A7R3_INPUT_H
#define A7R3_INPUT_H

/* Sony A7R3 physical button codes received on OSAL queue 0x007a0020 (I-InputMain) */
#define A7R3_KEY_UP    0x003u
#define A7R3_KEY_DOWN  0x030u
#define A7R3_KEY_LEFT  0x015u
#define A7R3_KEY_RIGHT 0x1afu
#define A7R3_KEY_ENTER 0x1acu
#define A7R3_KEY_MENU  0x1abu

/* Non-dismissing display / EVF / LCD-tilt events on QID_INPUT_MAIN */
#define A7R3_KEY_EYE_CONTACT  0x708bu /* KEY_OTHER_EYE_CONTACT (EVF eye sensor) */
#define A7R3_KEY_FINDER_LCD   0x708fu /* KEY_SLI_FINDER_LCD */
#define A7R3_KEY_EVF_OPEN     0x01d5u /* SLIKEY_EVF_OPEN */
#define A7R3_KEY_EVF_CLOSE    0x01d6u /* SLIKEY_EVF_CLOSE */
#define A7R3_KEY_PANEL_TILT   0x70efu /* KEY_PANEL_TILT_DET */

/* Physical EV compensation dial codes */
#define A7R3_KEY_EV_DIAL1     0x70d2u /* KEY_SLI_EV_COMPENSATION */
#define A7R3_KEY_EV_DIAL2     0x70d3u /* KEY_SLI_EV_COMPENSATION2 */

struct a7r3_key_ev {
    unsigned code;
    unsigned status; /* 1 = press, 0 = release */
};

struct a7r3_touch_ev {
    int x;           /* calibrated 0..639 */
    int y;           /* calibrated 0..479 */
    int down;        /* 1 = touch press edge, 0 = release */
};

/* Install / remove atomic libObj.so osal_rcv_msg_tmo .got hook.
 * Always call a7r3_input_remove() on clean shutdown before exiting worker thread. */
int  a7r3_input_install(void);
void a7r3_input_remove(void);

/* Control whether physical keys and touches are swallowed from the native app
 * (e.g. set (0, 0) while add-on menu is closed, (1, 1) while open). */
void a7r3_input_set_swallow(int swallow_keys, int swallow_touch);
void a7r3_input_flush(void);

/* Poll next buffered key or touch event from SPSC ring (non-blocking, returns 1 if event). */
int  a7r3_input_poll_key(struct a7r3_key_ev *out);
int  a7r3_input_poll_touch(struct a7r3_touch_ev *out);

/* Synchronous callback invoked when any non-navigation event arrives while keys are swallowed.
 * Allows immediate auto-dismiss and native framebuffer restoration before returning event to im.elf. */
typedef void (*a7r3_ext_dismiss_cb)(unsigned code, unsigned status);
void a7r3_input_set_ext_dismiss_cb(a7r3_ext_dismiss_cb cb);
int  a7r3_input_take_ext_dismiss(unsigned *out_code, unsigned *out_status);
int  a7r3_input_take_disp_switch(unsigned *out_code, unsigned *out_status);

/* Shield physical EV compensation dial (0x70d2/0x70d3) so firmware always sees 0. */
void a7r3_input_set_ev_dial_shield(int enable);
int  a7r3_input_get_ev_dial_shield(void);
int  a7r3_input_take_ev_raw(unsigned *out_code, unsigned *out_w1, unsigned *out_w2);
unsigned a7r3_input_native_activity_seq(void);

#endif /* A7R3_INPUT_H */
