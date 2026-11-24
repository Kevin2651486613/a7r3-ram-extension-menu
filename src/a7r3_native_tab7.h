#ifndef A7R3_NATIVE_TAB7_H
#define A7R3_NATIVE_TAB7_H

/*
 * Native Sony UI 7th top-level category (Tab 6: "拓展菜单") for ILCE-7RM3 v3.10.
 *
 * Route A1 pure-RAM design (zero flash writes, zero .text patching, zero .uxc binary blobs):
 *   1. Extends viewUnified2.so active CmnViewSettingNode root (0x00a51c60) from 6 to 7 tabs.
 *   2. Hooks PAS_MenuListTab (vtable slot 0x008bcb7c) to post-place Sony's reserved 7th tab
 *      icon & focus bar widgets at {533, 0, 91, 66} (avoiding overlap with tabs 0..5).
 *   3. Swaps CmnViewProcessDataMgr singleton vptr (0x00a48b10) to handle custom proc_ids
 *      (0x7f01..0x7f09) entirely in pure RAM without touching camera Backup/Model storage.
 *   4. Hooks libObj.so TextSprite::SetString(res_id) GOT (0x0121aa9c) to resolve synthetic
 *      string IDs (0x7f010000..0x7f0100ff) into UTF-8 labels rendered by Sony's native font.
 */
int  a7r3_native_tab7_install(void);
void a7r3_native_tab7_remove(void);
int  a7r3_native_tab7_take_stats(unsigned *out_mlt_hits, unsigned *out_mgr_hits,
                                 unsigned *out_text_hits);

#endif /* A7R3_NATIVE_TAB7_H */
