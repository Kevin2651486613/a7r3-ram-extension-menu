#ifndef A7R3_PB_FOCUS_H
#define A7R3_PB_FOCUS_H

/* Install / remove atomic hooks for Playback AF Focus Point Display.
 * Hooks libmpr FeaCore::SendCallback and libObj EnterStillPb/ExitStillPb GOTs. */
int  a7r3_pb_focus_install(void);
void a7r3_pb_focus_remove(void);

/* Enable or disable rendering the AF reticle over playback images. */
void a7r3_pb_focus_set_enabled(int enable);
int  a7r3_pb_focus_get_enabled(void);

/* Worker-thread tick (safe for logging and OSD reticle updates). */
void a7r3_pb_focus_tick(int md3_open);

/* Forwarded from ViewBaseProduct::UserGetLayoutMode(dev == 2) hook. */
void a7r3_pb_focus_on_disp_mode(int mode);

/* Lock-free visibility check safe to call from GPU ioctl hook. */
int  a7r3_pb_focus_allow_stamp(void);

#endif /* A7R3_PB_FOCUS_H */
