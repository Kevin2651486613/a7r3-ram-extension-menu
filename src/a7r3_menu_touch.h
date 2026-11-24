#ifndef A7R3_MENU_TOUCH_H
#define A7R3_MENU_TOUCH_H

/* Pure-RAM in-process menu touch controller for A7R3 v3.10.
 * Zero .text patching and zero heap scanning: tracks active Sony UI views
 * via RW vtable hooks and injects native OSAL input queue events. */
int  a7r3_menu_touch_install(void);
void a7r3_menu_touch_remove(void);

void a7r3_menu_touch_set_enabled(int enable);
int  a7r3_menu_touch_get_enabled(void);

/* Invoked on Sony's UI thread from PAS_MenuListTab update hook to keep
 * the active category, subpage, and visible row count accurately synced. */
void a7r3_menu_touch_notify_mlt(void *mlt, int cur_cat, int cur_page, int cur_row_cnt);

/* Poll touch taps and drive menu navigation/confirm/cancel on worker thread. */
void a7r3_menu_touch_tick(void);

int  a7r3_menu_touch_take_stats(unsigned *out_taps, unsigned *out_actions);

#endif /* A7R3_MENU_TOUCH_H */
