#ifndef A7R3_SHUTTER_SFX_H
#define A7R3_SHUTTER_SFX_H

#define A7R3_SFX_BUILTIN_CNT  1
#define A7R3_SFX_MAX_SD_FILES 16
#define A7R3_SFX_MAX_TOTAL    (A7R3_SFX_BUILTIN_CNT + A7R3_SFX_MAX_SD_FILES)

#define A7R3_SFX_MODE_OFF     0
#define A7R3_SFX_MODE_MECH    1
#define A7R3_SFX_MODE_ESHUT   2
#define A7R3_SFX_MODE_BOTH    3

int  a7r3_shutter_sfx_install(void);
void a7r3_shutter_sfx_uninstall(void);

void a7r3_shutter_sfx_set_mode(int mode);
int  a7r3_shutter_sfx_get_mode(void);
void a7r3_shutter_sfx_set_enabled(int on);
int  a7r3_shutter_sfx_get_enabled(void);

int  a7r3_shutter_sfx_is_eshutter(void);
void a7r3_shutter_sfx_refresh_state(void);

int  a7r3_shutter_sfx_rescan_sd(void);
int  a7r3_shutter_sfx_rescan_sd_sync(void);
int  a7r3_shutter_sfx_count(void);
int  a7r3_shutter_sfx_sd_count(void);
int  a7r3_shutter_sfx_get_selected(void);
int  a7r3_shutter_sfx_select(int idx);
const char *a7r3_shutter_sfx_name(int idx);

void a7r3_shutter_sfx_set_volume(int level);
int  a7r3_shutter_sfx_get_volume(void);

void a7r3_shutter_sfx_preview(void);
void a7r3_shutter_sfx_reset(void);

#endif /* A7R3_SHUTTER_SFX_H */
