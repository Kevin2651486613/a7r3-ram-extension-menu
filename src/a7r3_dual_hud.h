#ifndef A7R3_DUAL_HUD_H
#define A7R3_DUAL_HUD_H

/* Install / remove atomic GOT and vtable hooks for simultaneous Level + Histogram HUD.
 * Must be called from im.elf process context; call a7r3_dual_hud_remove() on quit. */
int  a7r3_dual_hud_install(void);
void a7r3_dual_hud_remove(void);

/* Enable or disable simultaneous Level + Histogram in LCD Level mode (Mode 4). */
void a7r3_dual_hud_set_enabled(int enable);
int  a7r3_dual_hud_get_enabled(void);

/* Poll runtime hit counters for diagnostic logging; returns 1 if any changed. */
int  a7r3_dual_hud_take_stats(unsigned *out_parse, unsigned *out_gate1, unsigned *out_route);

#endif /* A7R3_DUAL_HUD_H */
