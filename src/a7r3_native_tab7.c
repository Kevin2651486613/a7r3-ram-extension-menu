#include "a7r3_native_tab7.h"
#include "a7r3_dual_hud.h"
#include "a7r3_input.h"
#include "a7r3_menu_touch.h"
#include "a7r3_pb_focus.h"
#include "a7r3_shutter_sfx.h"
#include "a7r3_sys.h"

/* Verified A7R3 v3.10 viewUnified2.so & libObj.so offsets */
#define VU2_MENU_ROOT_OFF         0x00a51c60UL
#define VU2_MLT_UPDATE_SLOT       0x008bcb7cUL /* PAS_MenuListTab vtable +0x1ac */
#define VU2_MLT_UPDATE_REAL       0x0056ef3dUL /* Thumb */
#define VU2_MLT_PLACE_WGT_REAL    0x0056d71bUL /* Thumb */
#define VU2_MLT_GET_ROW_CNT_REAL  0x0056d5c3UL /* Thumb: int(mlt*, int cat, int page) */
#define VU2_MLT_TAB_COUNT_OFF     0x1c8u
#define VU2_MLT_TAB_BG_OFF        0x960u
#define VU2_MLT_TAB6_ICON_OFF     0x1840u      /* 0xb80 + 6 * 0x220 */
#define VU2_MLT_TAB6_BAR_OFF      0x2720u      /* 0x1a60 + 6 * 0x220 */
#define GEN_ICON_SET_IMG_VT_IDX   (0x104u / 4u)
#define SONY_TAB_BG_6TAB          0x020cc146u

#define VU2_PROC_MGR_GUARD        0x00a48b0cUL
#define VU2_PROC_MGR_INST         0x00a48b10UL
#define VU2_PROC_MGR_VPTR         0x00867ae0UL
#define PROC_MGR_VT_COPY_WORDS    28u          /* -2 RTTI/offset words + 26 slots */

#define LIBOBJ_TEXT_SETSTR_ID_GOT    0x0121aa9cUL
#define LIBOBJ_TEXT_SETSTR_ID_REAL   0x005dbe09UL /* Thumb */
#define LIBOBJ_TEXT_SETSTR_CSTR_REAL 0x005dbfa1UL /* Thumb */
#define LIBOBJ_ICON_SET_SCALING      0x003cfc4fUL /* Thumb: GEN_Icon::setScalingFlag(bool) */
#define LIBOBJ_ICON_SET_FILTER       0x003cfe91UL /* Thumb: GEN_Icon::setImageFilter(bool) */
#define LIBOBJ_RES_GET_IMG_SIZE      0x005c63e5UL /* Thumb: ResourceManager::getImageSize */

#define SONY_TAB7_BG_NORM            0x0205d9b5u
#define SONY_TAB7_FG_ICON            0x0205cb79u

/* Sony native string / icon IDs reused for ON/OFF option rows */
#define SONY_STR_ON               0x0204e109u
#define SONY_STR_OFF              0x0204e0d2u
#define SONY_ICON_ON              0x020ce47eu
#define SONY_ICON_OFF             0x020ce47cu
#define SONY_STAT_ON              0x020ce47du
#define SONY_STAT_OFF             0x020ce491u

/* Custom virtual proc_ids (far above Sony's 0x259 range) */
#define PROC_ID_EXT_BASE          0x7f01
#define PROC_ID_EXT_EV_SHIELD     0x7f01
#define PROC_ID_EXT_DUAL_HUD      0x7f02
#define PROC_ID_EXT_PB_FOCUS      0x7f03
#define PROC_ID_EXT_SFX_ENABLE    0x7f04
#define PROC_ID_EXT_SFX_VOL       0x7f05
#define PROC_ID_EXT_SFX_PREVIEW   0x7f06
#define PROC_ID_EXT_SFX_SELECT    0x7f07
#define PROC_ID_EXT_SFX_RESCAN    0x7f08
#define PROC_ID_EXT_RESET_ALL     0x7f09
#define PROC_ID_EXT_MENU_TOUCH    0x7f0a
#define PROC_ID_EXT_MAX           0x7f0a

/* Custom synthetic string resource IDs */
#define STR_SYN_BASE              0x7f010000u
#define STR_SYN_TAB_TITLE         0x7f010001u
#define STR_SYN_EV_SHIELD_T       0x7f010002u
#define STR_SYN_EV_SHIELD_G       0x7f010003u
#define STR_SYN_DUAL_HUD_T        0x7f010004u
#define STR_SYN_DUAL_HUD_G        0x7f010005u
#define STR_SYN_PB_FOCUS_T        0x7f010006u
#define STR_SYN_PB_FOCUS_G        0x7f010007u
#define STR_SYN_SFX_EN_T          0x7f010008u
#define STR_SYN_SFX_EN_G          0x7f010009u
#define STR_SYN_SFX_VOL_T         0x7f01000au
#define STR_SYN_SFX_VOL_G         0x7f01000bu
#define STR_SYN_SFX_PREV_T        0x7f01000cu
#define STR_SYN_SFX_PREV_G        0x7f01000du
#define STR_SYN_SFX_SEL_T         0x7f01000eu
#define STR_SYN_SFX_SEL_G         0x7f01000fu
#define STR_SYN_SFX_SCAN_T        0x7f010010u
#define STR_SYN_SFX_SCAN_G        0x7f010011u
#define STR_SYN_RESET_T           0x7f010012u
#define STR_SYN_RESET_G           0x7f010013u
#define STR_SYN_VOL_LOW           0x7f010014u
#define STR_SYN_VOL_MID           0x7f010015u
#define STR_SYN_VOL_HIGH          0x7f010016u
#define STR_SYN_TOUCH_T           0x7f010017u
#define STR_SYN_TOUCH_G           0x7f010018u
#define STR_SYN_SFX_ITEM_BASE     0x7f010080u

/* Exact 40-byte A7R3 CmnViewSettingNode layout */
struct cmn_setting_prop_rec {
    int32_t key;
    int32_t kind;
    uint32_t value;
};

struct cmn_setting_props {
    const struct cmn_setting_prop_rec *recs;
    int count;
};

struct cmn_setting_node {
    unsigned vptr;
    void **children;
    int count;
    const struct cmn_setting_props *properties;
    void *parent;
    int depth;
    int selected;
    int selectable;
    int physical;
    unsigned state;
};

typedef int  (*mlt_update_fn)(void *self);
typedef void (*mlt_place_wgt_fn)(void *self, void *widget, const int16_t rect[4], int visible);
typedef int  (*mlt_get_row_cnt_fn)(void *self, int cat, int page);
typedef void (*gen_icon_set_img_fn)(void *icon, unsigned res_id, int flag);
typedef void (*gen_icon_set_bool_fn)(void *icon, int flag);
typedef void (*res_get_img_size_fn)(unsigned res_id, uint16_t *out_w, uint16_t *out_h);
typedef void (*text_setstr_id_fn)(void *sprite, unsigned res_id);
typedef void (*text_setstr_cstr_fn)(void *sprite, const char *utf8);

typedef int (*mgr_get_value_fn)(void *mgr, int proc_id, int *out_val);
typedef int (*mgr_set_value_fn)(void *mgr, int proc_id, int val);
typedef int (*mgr_query_fn)(void *mgr, int proc_id);
typedef int (*mgr_query_val_fn)(void *mgr, int proc_id, int val);
typedef int (*mgr_exec_fn)(void *mgr, int proc_id);

/* Hook state */
static int g_tab7_installed;
static unsigned long g_vu2_base;
static unsigned long g_obj_base;

static struct cmn_setting_node *g_root_node;
static void **g_orig_root_children;
static int g_orig_root_count;
static void *g_root_children7[7];

static volatile unsigned long *g_mlt_update_slot;
static unsigned long g_orig_mlt_update;
static mlt_update_fn g_real_mlt_update;
static mlt_place_wgt_fn g_mlt_place_wgt;
static mlt_get_row_cnt_fn g_mlt_get_row_cnt;
static gen_icon_set_bool_fn g_icon_set_scaling;
static gen_icon_set_bool_fn g_icon_set_filter;
static uint16_t g_tab7_fg_w;
static uint16_t g_tab7_fg_h;

static unsigned long g_mgr_vt_raw[PROC_MGR_VT_COPY_WORDS];
static volatile unsigned long *g_mgr_vptr_slot;
static unsigned long g_orig_mgr_vptr;
static mgr_get_value_fn g_real_get_value;
static mgr_set_value_fn g_real_set_value;
static mgr_query_fn     g_real_is_grayout;
static mgr_query_val_fn g_real_is_grayout_val;
static mgr_query_fn     g_real_is_disable;
static mgr_query_val_fn g_real_is_disable_val;
static mgr_exec_fn      g_real_exec;

static volatile unsigned long *g_text_setstr_got;
static unsigned long g_orig_text_setstr_got;
static text_setstr_id_fn g_real_text_setstr_id;
static text_setstr_cstr_fn g_real_text_setstr_cstr;

static volatile unsigned g_mlt_hits;
static volatile unsigned g_mgr_hits;
static volatile unsigned g_text_hits;
static unsigned g_last_mlt_hits;
static unsigned g_last_mgr_hits;
static unsigned g_last_text_hits;

/* =========================================================================
 * Custom CmnViewSettingNode Tree for Tab 6 (2 Pages, 9 Native Setting Items)
 * ========================================================================= */

static const struct cmn_setting_prop_rec g_tab7_recs[] = {
    { 0x00, 0, 0xffffffffu },
    { 0x02, 0, SONY_TAB7_FG_ICON }, /* Foreground white icon overlay (paired with 0x0205d9b5/b7/b4) */
    { 0x06, 0, 0xffffffffu },
    { 0x13, 1, 2u },                /* Tab theme group */
    { 0x1d, 0, SONY_TAB7_BG_NORM }, /* Sony unused 7th tab background normal */
    { 0x1e, 0, 0x0205d9b7u },       /* Sony unused 7th tab background active */
    { 0x1f, 0, 0x0205d9b4u },       /* Sony unused 7th tab background highlight */
};
static const struct cmn_setting_props g_tab7_props = { g_tab7_recs, 7 };

static const struct cmn_setting_prop_rec g_page1_recs[] = {
    { 0x00, 0, STR_SYN_TAB_TITLE },
    { 0x06, 0, STR_SYN_TAB_TITLE },
    { 0x0b, 1, 1u },
};
static const struct cmn_setting_props g_page1_props = { g_page1_recs, 3 };

static const struct cmn_setting_prop_rec g_page2_recs[] = {
    { 0x00, 0, STR_SYN_TAB_TITLE },
    { 0x06, 0, STR_SYN_TAB_TITLE },
    { 0x0b, 1, 2u },
};
static const struct cmn_setting_props g_page2_props = { g_page2_recs, 3 };

#define DECL_TOGGLE_ITEM(TAG, STR_T, STR_G, PROC_ID) \
    static const struct cmn_setting_prop_rec TAG##_recs[] = { \
        { 0x00, 0, (STR_T) }, \
        { 0x06, 0, (STR_G) }, \
        { 0x0b, 1, 2u }, \
        { 0x0c, 1, 0u }, /* style 0: level-2 option list popup */ \
        { 0x0e, 1, (uint32_t)(PROC_ID) }, \
        { 0x20, 1, 0u }, \
    }; \
    static const struct cmn_setting_props TAG##_props = { TAG##_recs, 6 };

#define DECL_EXEC_ITEM(TAG, STR_T, STR_G, PROC_ID) \
    static const struct cmn_setting_prop_rec TAG##_recs[] = { \
        { 0x00, 0, (STR_T) }, \
        { 0x06, 0, (STR_G) }, \
        { 0x0b, 1, 2u }, \
        { 0x0c, 1, 4u }, /* style 4: direct action item */ \
        { 0x0e, 1, (uint32_t)(PROC_ID) }, \
        { 0x20, 1, 0u }, \
    }; \
    static const struct cmn_setting_props TAG##_props = { TAG##_recs, 6 };

DECL_TOGGLE_ITEM(g_p1_ev,   STR_SYN_EV_SHIELD_T, STR_SYN_EV_SHIELD_G, PROC_ID_EXT_EV_SHIELD)
DECL_TOGGLE_ITEM(g_p1_dual, STR_SYN_DUAL_HUD_T,  STR_SYN_DUAL_HUD_G,  PROC_ID_EXT_DUAL_HUD)
DECL_TOGGLE_ITEM(g_p1_pb,   STR_SYN_PB_FOCUS_T,  STR_SYN_PB_FOCUS_G,  PROC_ID_EXT_PB_FOCUS)
DECL_TOGGLE_ITEM(g_p1_tch,  STR_SYN_TOUCH_T,     STR_SYN_TOUCH_G,     PROC_ID_EXT_MENU_TOUCH)
DECL_TOGGLE_ITEM(g_p1_sfx,  STR_SYN_SFX_EN_T,    STR_SYN_SFX_EN_G,    PROC_ID_EXT_SFX_ENABLE)
DECL_TOGGLE_ITEM(g_p1_vol,  STR_SYN_SFX_VOL_T,   STR_SYN_SFX_VOL_G,   PROC_ID_EXT_SFX_VOL)

DECL_TOGGLE_ITEM(g_p2_sel,  STR_SYN_SFX_SEL_T,   STR_SYN_SFX_SEL_G,   PROC_ID_EXT_SFX_SELECT)
DECL_EXEC_ITEM  (g_p2_prv,  STR_SYN_SFX_PREV_T,  STR_SYN_SFX_PREV_G,  PROC_ID_EXT_SFX_PREVIEW)
DECL_EXEC_ITEM  (g_p2_scan, STR_SYN_SFX_SCAN_T,  STR_SYN_SFX_SCAN_G,  PROC_ID_EXT_SFX_RESCAN)
DECL_EXEC_ITEM  (g_p2_rst,  STR_SYN_RESET_T,     STR_SYN_RESET_G,     PROC_ID_EXT_RESET_ALL)

#define DECL_OPT_REC(TAG, STR_T, STR_G, ICON, STAT, VAL) \
    static const struct cmn_setting_prop_rec TAG##_recs[] = { \
        { 0x00, 0, (STR_T) }, \
        { 0x01, 0, (ICON) }, \
        { 0x05, 0, (STAT) }, \
        { 0x06, 0, (STR_G) }, \
        { 0x0b, 1, 0u }, \
        { 0x0c, 1, 1u }, \
        { 0x0f, 1, (uint32_t)(VAL) }, \
        { 0x10, 1, (uint32_t)(VAL) }, \
    }; \
    static const struct cmn_setting_props TAG##_props = { TAG##_recs, 8 };

/* Shared ON/OFF option children for binary toggles */
DECL_OPT_REC(g_opt_on_ev,   SONY_STR_ON,  STR_SYN_EV_SHIELD_G, SONY_ICON_ON,  SONY_STAT_ON,  0u)
DECL_OPT_REC(g_opt_off_ev,  SONY_STR_OFF, STR_SYN_EV_SHIELD_G, SONY_ICON_OFF, SONY_STAT_OFF, 1u)
DECL_OPT_REC(g_opt_on_dh,   SONY_STR_ON,  STR_SYN_DUAL_HUD_G,  SONY_ICON_ON,  SONY_STAT_ON,  0u)
DECL_OPT_REC(g_opt_off_dh,  SONY_STR_OFF, STR_SYN_DUAL_HUD_G,  SONY_ICON_OFF, SONY_STAT_OFF, 1u)
DECL_OPT_REC(g_opt_on_pb,   SONY_STR_ON,  STR_SYN_PB_FOCUS_G,  SONY_ICON_ON,  SONY_STAT_ON,  0u)
DECL_OPT_REC(g_opt_off_pb,  SONY_STR_OFF, STR_SYN_PB_FOCUS_G,  SONY_ICON_OFF, SONY_STAT_OFF, 1u)
DECL_OPT_REC(g_opt_on_tch,  SONY_STR_ON,  STR_SYN_TOUCH_G,     SONY_ICON_ON,  SONY_STAT_ON,  0u)
DECL_OPT_REC(g_opt_off_tch, SONY_STR_OFF, STR_SYN_TOUCH_G,     SONY_ICON_OFF, SONY_STAT_OFF, 1u)
DECL_OPT_REC(g_opt_on_sfx,  SONY_STR_ON,  STR_SYN_SFX_EN_G,    SONY_ICON_ON,  SONY_STAT_ON,  0u)
DECL_OPT_REC(g_opt_off_sfx, SONY_STR_OFF, STR_SYN_SFX_EN_G,    SONY_ICON_OFF, SONY_STAT_OFF, 1u)

/* Volume options (0=Low, 1=Mid, 2=High) */
DECL_OPT_REC(g_opt_vol0, STR_SYN_VOL_LOW,  STR_SYN_SFX_VOL_G, 0u, 0u, 0u)
DECL_OPT_REC(g_opt_vol1, STR_SYN_VOL_MID,  STR_SYN_SFX_VOL_G, 0u, 0u, 1u)
DECL_OPT_REC(g_opt_vol2, STR_SYN_VOL_HIGH, STR_SYN_SFX_VOL_G, 0u, 0u, 2u)

/* Sound track dynamic option table (0..18) */
static struct cmn_setting_prop_rec g_sfx_opt_recs[A7R3_SFX_MAX_TOTAL][8];
static struct cmn_setting_props g_sfx_opt_props[A7R3_SFX_MAX_TOTAL];
static struct cmn_setting_node g_sfx_opt_nodes[A7R3_SFX_MAX_TOTAL];
static void *g_sfx_opt_ptrs[A7R3_SFX_MAX_TOTAL];

static struct cmn_setting_node g_opt_nodes_onoff[10];
static void *g_ev_opt_ptrs[2]    = { &g_opt_nodes_onoff[0], &g_opt_nodes_onoff[1] };
static void *g_dh_opt_ptrs[2]    = { &g_opt_nodes_onoff[2], &g_opt_nodes_onoff[3] };
static void *g_pb_opt_ptrs[2]    = { &g_opt_nodes_onoff[4], &g_opt_nodes_onoff[5] };
static void *g_tch_opt_ptrs[2]   = { &g_opt_nodes_onoff[6], &g_opt_nodes_onoff[7] };
static void *g_sfx_en_ptrs[2]  = { &g_opt_nodes_onoff[8], &g_opt_nodes_onoff[9] };

static struct cmn_setting_node g_vol_nodes[3];
static void *g_vol_ptrs[3] = { &g_vol_nodes[0], &g_vol_nodes[1], &g_vol_nodes[2] };

static struct cmn_setting_node g_p1_nodes[6];
static void *g_p1_ptrs[6] = {
    &g_p1_nodes[0], &g_p1_nodes[1], &g_p1_nodes[2],
    &g_p1_nodes[3], &g_p1_nodes[4], &g_p1_nodes[5]
};

static struct cmn_setting_node g_p2_nodes[4];
static void *g_p2_ptrs[4] = {
    &g_p2_nodes[0], &g_p2_nodes[1], &g_p2_nodes[2], &g_p2_nodes[3]
};

static struct cmn_setting_node g_page_nodes[2];
static void *g_tab7_page_ptrs[2] = { &g_page_nodes[0], &g_page_nodes[1] };

static struct cmn_setting_node g_tab7_node;

static void zero_node(struct cmn_setting_node *n, unsigned vptr,
                      void **children, int count,
                      const struct cmn_setting_props *props)
{
    n->vptr = vptr;
    n->children = children;
    n->count = count;
    n->properties = props;
    n->parent = NULL;
    n->depth = 0;
    n->selected = -1;
    n->selectable = 1;
    n->physical = -1;
    n->state = 0u;
}

static void build_native_tree_nodes(unsigned node_vptr)
{
    unsigned i;
    static const struct cmn_setting_props *const onoff_props[10] = {
        &g_opt_on_ev_props,  &g_opt_off_ev_props,
        &g_opt_on_dh_props,  &g_opt_off_dh_props,
        &g_opt_on_pb_props,  &g_opt_off_pb_props,
        &g_opt_on_tch_props, &g_opt_off_tch_props,
        &g_opt_on_sfx_props, &g_opt_off_sfx_props,
    };

    for (i = 0u; i < 10u; i++)
        zero_node(&g_opt_nodes_onoff[i], node_vptr, NULL, 0, onoff_props[i]);

    zero_node(&g_vol_nodes[0], node_vptr, NULL, 0, &g_opt_vol0_props);
    zero_node(&g_vol_nodes[1], node_vptr, NULL, 0, &g_opt_vol1_props);
    zero_node(&g_vol_nodes[2], node_vptr, NULL, 0, &g_opt_vol2_props);

    for (i = 0u; i < A7R3_SFX_MAX_TOTAL; i++) {
        uint32_t sid = STR_SYN_SFX_ITEM_BASE + i;
        g_sfx_opt_recs[i][0] = (struct cmn_setting_prop_rec){ 0x00, 0, sid };
        g_sfx_opt_recs[i][1] = (struct cmn_setting_prop_rec){ 0x01, 0, 0u };
        g_sfx_opt_recs[i][2] = (struct cmn_setting_prop_rec){ 0x05, 0, 0u };
        g_sfx_opt_recs[i][3] = (struct cmn_setting_prop_rec){ 0x06, 0, STR_SYN_SFX_SEL_G };
        g_sfx_opt_recs[i][4] = (struct cmn_setting_prop_rec){ 0x0b, 1, 0u };
        g_sfx_opt_recs[i][5] = (struct cmn_setting_prop_rec){ 0x0c, 1, 1u };
        g_sfx_opt_recs[i][6] = (struct cmn_setting_prop_rec){ 0x0f, 1, i };
        g_sfx_opt_recs[i][7] = (struct cmn_setting_prop_rec){ 0x10, 1, i };
        g_sfx_opt_props[i].recs = g_sfx_opt_recs[i];
        g_sfx_opt_props[i].count = 8;
        zero_node(&g_sfx_opt_nodes[i], node_vptr, NULL, 0, &g_sfx_opt_props[i]);
        g_sfx_opt_ptrs[i] = &g_sfx_opt_nodes[i];
    }

    zero_node(&g_p1_nodes[0], node_vptr, g_ev_opt_ptrs,  2, &g_p1_ev_props);
    zero_node(&g_p1_nodes[1], node_vptr, g_dh_opt_ptrs,  2, &g_p1_dual_props);
    zero_node(&g_p1_nodes[2], node_vptr, g_pb_opt_ptrs,  2, &g_p1_pb_props);
    zero_node(&g_p1_nodes[3], node_vptr, g_tch_opt_ptrs, 2, &g_p1_tch_props);
    zero_node(&g_p1_nodes[4], node_vptr, g_sfx_en_ptrs,  2, &g_p1_sfx_props);
    zero_node(&g_p1_nodes[5], node_vptr, g_vol_ptrs,     3, &g_p1_vol_props);

    zero_node(&g_p2_nodes[0], node_vptr, g_sfx_opt_ptrs, A7R3_SFX_MAX_TOTAL, &g_p2_sel_props);
    zero_node(&g_p2_nodes[1], node_vptr, NULL,           0,                  &g_p2_prv_props);
    zero_node(&g_p2_nodes[2], node_vptr, NULL,           0,                  &g_p2_scan_props);
    zero_node(&g_p2_nodes[3], node_vptr, NULL,           0,                  &g_p2_rst_props);

    zero_node(&g_page_nodes[0], node_vptr, g_p1_ptrs, 6, &g_page1_props);
    zero_node(&g_page_nodes[1], node_vptr, g_p2_ptrs, 4, &g_page2_props);

    zero_node(&g_tab7_node, node_vptr, g_tab7_page_ptrs, 2, &g_tab7_props);
}

/* =========================================================================
 * Hook 1: TextSprite::SetString(res_id) -> resolve synthetic UTF-8 strings
 * ========================================================================= */

static const char *lookup_synthetic_utf8(unsigned res_id)
{
    if (res_id >= STR_SYN_SFX_ITEM_BASE &&
        res_id < STR_SYN_SFX_ITEM_BASE + A7R3_SFX_MAX_TOTAL) {
        int idx = (int)(res_id - STR_SYN_SFX_ITEM_BASE);
        return a7r3_shutter_sfx_name(idx);
    }

    switch (res_id) {
    case STR_SYN_TAB_TITLE:   return "拓展菜单";
    case STR_SYN_EV_SHIELD_T: return "屏蔽曝光补偿波轮";
    case STR_SYN_EV_SHIELD_G: return "将物理曝光补偿拨盘输入始终固定为 0";
    case STR_SYN_DUAL_HUD_T:  return "水平仪与直方图同显";
    case STR_SYN_DUAL_HUD_G:
        return a7r3_dual_hud_has_payload()
            ? "在电子水平仪模式下同步显示实时直方图"
            : "未编入本地 .local.h 载荷，当前为透传模式";
    case STR_SYN_PB_FOCUS_T:  return "相册显示焦点位置";
    case STR_SYN_PB_FOCUS_G:  return "回放照片时在画面上叠加标注对焦点位置";
    case STR_SYN_TOUCH_T:     return "菜单触摸控制";
    case STR_SYN_TOUCH_G:     return "开启后支持触摸原生菜单大类、翻页、列表、弹窗与白平衡";
    case STR_SYN_SFX_EN_T:    return "电子快门自定义音效";
    case STR_SYN_SFX_EN_G:    return "仅在电子快门(静音拍摄)模式下触发自定义快门音";
    case STR_SYN_SFX_VOL_T:   return "快门音效音量";
    case STR_SYN_SFX_VOL_G:   return "调节机身扬声器播放自定义快门音的音量";
    case STR_SYN_SFX_PREV_T:  return "试听当前快门音效";
    case STR_SYN_SFX_PREV_G:  return "立即通过机身扬声器试听当前选择的快门音效";
    case STR_SYN_SFX_SEL_T:   return "选择快门音效";
    case STR_SYN_SFX_SEL_G:   return "选择内置或内存卡 /SHUTTER_SFX/ 目录中的 WAV 音效";
    case STR_SYN_SFX_SCAN_T:  return "刷新SD卡音效列表";
    case STR_SYN_SFX_SCAN_G:  return "重新扫描卡1/卡2 /SHUTTER_SFX/ 下的 PCM WAV 文件";
    case STR_SYN_RESET_T:     return "恢复全部默认设置";
    case STR_SYN_RESET_G:     return "将拓展菜单所有增强开关与音效选项恢复为默认关闭";
    case STR_SYN_VOL_LOW:     return "低";
    case STR_SYN_VOL_MID:     return "中";
    case STR_SYN_VOL_HIGH:    return "高";
    default:                  return NULL;
    }
}

__attribute__((noinline))
static void tab7_text_setstr_hook(void *sprite, unsigned res_id)
{
    if (res_id >= STR_SYN_BASE && res_id <= 0x7f0100ffu && g_real_text_setstr_cstr) {
        const char *utf8 = lookup_synthetic_utf8(res_id);
        if (utf8) {
            g_text_hits++;
            g_real_text_setstr_cstr(sprite, utf8);
            return;
        }
    }
    g_real_text_setstr_id(sprite, res_id);
}

/* =========================================================================
 * Hook 2: PAS_MenuListTab layout update -> place reserved 7th tab widget
 * ========================================================================= */

__attribute__((noinline))
static int tab7_mlt_update_hook(void *self)
{
    int rc = g_real_mlt_update(self);
    if (self && g_mlt_place_wgt) {
        int tab_cnt = *(const int *)((const uint8_t *)self + VU2_MLT_TAB_COUNT_OFF);
        if (g_mlt_get_row_cnt) {
            int cur_cat = *(const int *)((const uint8_t *)self + 0x1d4u);
            int cur_page = *(const int *)((const uint8_t *)self + 0x1d8u);
            int row_cnt = g_mlt_get_row_cnt(self, cur_cat, cur_page);
            a7r3_menu_touch_notify_mlt(self, cur_cat, cur_page, row_cnt);
        }
        if (tab_cnt >= 7) {
            static const int16_t rect_tab6[4] = { 533, 0, 91, 66 };
            int16_t rect_bar6[4] = { 533, 0, 91, 66 };
            void *w_icon = (void *)((uint8_t *)self + VU2_MLT_TAB6_ICON_OFF);
            void *w_bar  = (void *)((uint8_t *)self + VU2_MLT_TAB6_BAR_OFF);
            void *w_bg   = (void *)((uint8_t *)self + VU2_MLT_TAB_BG_OFF);

            if (g_icon_set_scaling)
                g_icon_set_scaling(w_icon, 1);
            if (g_icon_set_filter)
                g_icon_set_filter(w_icon, 1);
            g_mlt_place_wgt(self, w_icon, rect_tab6, 1);

            if (g_tab7_fg_w > 0u && g_tab7_fg_w <= 160u &&
                g_tab7_fg_h > 0u && g_tab7_fg_h <= 100u) {
                rect_bar6[0] = (int16_t)(533 + ((91 - (int)g_tab7_fg_w) / 2));
                rect_bar6[1] = (int16_t)((66 - (int)g_tab7_fg_h) / 2);
                rect_bar6[2] = (int16_t)g_tab7_fg_w;
                rect_bar6[3] = (int16_t)g_tab7_fg_h;
            }
            if (g_icon_set_scaling)
                g_icon_set_scaling(w_bar, 0);
            g_mlt_place_wgt(self, w_bar, rect_bar6, 1);

            if (a7r3_pm_readable(w_bg, 4u)) {
                const unsigned long *vt = (const unsigned long *)(uintptr_t)(*(const unsigned long *)w_bg);
                if (vt && a7r3_pm_readable(vt, (GEN_ICON_SET_IMG_VT_IDX + 1u) * 4u)) {
                    gen_icon_set_img_fn set_img = (gen_icon_set_img_fn)vt[GEN_ICON_SET_IMG_VT_IDX];
                    if (set_img)
                        set_img(w_bg, SONY_TAB_BG_6TAB, 0);
                }
            }
            g_mlt_hits++;
        }
    }
    return rc;
}

/* =========================================================================
 * Hook 3: CmnViewProcessDataMgr vtable -> pure-RAM get/set/disable/exec
 * ========================================================================= */

static int is_ext_proc(int proc_id)
{
    return proc_id >= PROC_ID_EXT_BASE && proc_id <= PROC_ID_EXT_MAX;
}

__attribute__((noinline))
static int mgr_get_value_hook(void *mgr, int proc_id, int *out_val)
{
    if (is_ext_proc(proc_id)) {
        g_mgr_hits++;
        if (!out_val)
            return 0;
        switch (proc_id) {
        case PROC_ID_EXT_EV_SHIELD:
            *out_val = a7r3_input_get_ev_dial_shield() ? 0 : 1;
            return 1;
        case PROC_ID_EXT_DUAL_HUD:
            *out_val = a7r3_dual_hud_get_enabled() ? 0 : 1;
            return 1;
        case PROC_ID_EXT_PB_FOCUS:
            *out_val = a7r3_pb_focus_get_enabled() ? 0 : 1;
            return 1;
        case PROC_ID_EXT_MENU_TOUCH:
            *out_val = a7r3_menu_touch_get_enabled() ? 0 : 1;
            return 1;
        case PROC_ID_EXT_SFX_ENABLE:
            *out_val = a7r3_shutter_sfx_get_enabled() ? 0 : 1;
            return 1;
        case PROC_ID_EXT_SFX_VOL:
            *out_val = a7r3_shutter_sfx_get_volume();
            return 1;
        case PROC_ID_EXT_SFX_SELECT:
            *out_val = a7r3_shutter_sfx_get_selected();
            return 1;
        default:
            *out_val = 0;
            return 1;
        }
    }
    return g_real_get_value(mgr, proc_id, out_val);
}

__attribute__((noinline))
static int mgr_set_value_hook(void *mgr, int proc_id, int val)
{
    if (is_ext_proc(proc_id)) {
        g_mgr_hits++;
        a7r3_log_hex("SDK_TAB7_SET=", ((unsigned long)(unsigned)proc_id << 16) | ((unsigned)val & 0xffffu));
        switch (proc_id) {
        case PROC_ID_EXT_EV_SHIELD:
            a7r3_input_set_ev_dial_shield(val == 0 ? 1 : 0);
            return 1;
        case PROC_ID_EXT_DUAL_HUD:
            a7r3_dual_hud_set_enabled(val == 0 ? 1 : 0);
            return 1;
        case PROC_ID_EXT_PB_FOCUS:
            a7r3_pb_focus_set_enabled(val == 0 ? 1 : 0);
            return 1;
        case PROC_ID_EXT_MENU_TOUCH:
            a7r3_menu_touch_set_enabled(val == 0 ? 1 : 0);
            return 1;
        case PROC_ID_EXT_SFX_ENABLE:
            a7r3_shutter_sfx_set_enabled(val == 0 ? 1 : 0);
            return 1;
        case PROC_ID_EXT_SFX_VOL:
            a7r3_shutter_sfx_set_volume(val);
            return 1;
        case PROC_ID_EXT_SFX_SELECT:
            a7r3_shutter_sfx_select(val);
            return 1;
        default:
            return 1;
        }
    }
    return g_real_set_value(mgr, proc_id, val);
}

__attribute__((noinline))
static int mgr_is_grayout_hook(void *mgr, int proc_id)
{
    if (proc_id == PROC_ID_EXT_DUAL_HUD && !a7r3_dual_hud_has_payload())
        return 1;
    if (is_ext_proc(proc_id))
        return 0;
    return g_real_is_grayout(mgr, proc_id);
}

__attribute__((noinline))
static int mgr_is_grayout_val_hook(void *mgr, int proc_id, int val)
{
    if (proc_id == PROC_ID_EXT_DUAL_HUD && !a7r3_dual_hud_has_payload())
        return 1;
    if (is_ext_proc(proc_id))
        return 0;
    return g_real_is_grayout_val(mgr, proc_id, val);
}

__attribute__((noinline))
static int mgr_is_disable_hook(void *mgr, int proc_id)
{
    if (is_ext_proc(proc_id))
        return 0;
    return g_real_is_disable(mgr, proc_id);
}

__attribute__((noinline))
static int mgr_is_disable_val_hook(void *mgr, int proc_id, int val)
{
    if (proc_id == PROC_ID_EXT_SFX_SELECT) {
        int cnt = a7r3_shutter_sfx_count();
        return (val >= 0 && val < cnt) ? 0 : 1;
    }
    if (is_ext_proc(proc_id))
        return 0;
    return g_real_is_disable_val(mgr, proc_id, val);
}

__attribute__((noinline))
static int mgr_exec_hook(void *mgr, int proc_id)
{
    if (is_ext_proc(proc_id)) {
        g_mgr_hits++;
        a7r3_log_hex("SDK_TAB7_EXEC=", (unsigned long)(unsigned)proc_id);
        switch (proc_id) {
        case PROC_ID_EXT_SFX_PREVIEW:
            a7r3_shutter_sfx_preview();
            return 1;
        case PROC_ID_EXT_SFX_RESCAN:
            a7r3_shutter_sfx_rescan_sd();
            return 1;
        case PROC_ID_EXT_RESET_ALL:
            a7r3_input_set_ev_dial_shield(0);
            a7r3_dual_hud_set_enabled(0);
            a7r3_pb_focus_set_enabled(0);
            a7r3_menu_touch_set_enabled(0);
            a7r3_shutter_sfx_reset();
            return 1;
        default:
            return 1;
        }
    }
    return g_real_exec(mgr, proc_id);
}

/* =========================================================================
 * Public Install / Remove / Diagnostics
 * ========================================================================= */

int a7r3_native_tab7_install(void)
{
    unsigned long vu2_base, obj_base;
    struct cmn_setting_node *root;
    volatile unsigned char *mgr_guard;
    volatile unsigned long *mgr_inst;
    volatile unsigned long *mlt_slot;
    volatile unsigned long *text_slot;
    const unsigned long *orig_mgr_vt;
    unsigned i;

    if (g_tab7_installed)
        return 0;

    vu2_base = a7r3_mod_base("/viewUnified2.so");
    obj_base = a7r3_mod_base("/libObj.so");
    if (!vu2_base || !obj_base) {
        a7r3_log_str("SDK_TAB7_NO_MOD\n");
        return -1;
    }
    g_vu2_base = vu2_base;
    g_obj_base = obj_base;
    g_icon_set_scaling = (gen_icon_set_bool_fn)(obj_base + LIBOBJ_ICON_SET_SCALING);
    g_icon_set_filter  = (gen_icon_set_bool_fn)(obj_base + LIBOBJ_ICON_SET_FILTER);

    {
        res_get_img_size_fn get_sz = (res_get_img_size_fn)(obj_base + LIBOBJ_RES_GET_IMG_SIZE);
        uint16_t bg_w = 0u, bg_h = 0u, ref_w = 0u, ref_h = 0u;
        g_tab7_fg_w = 0u;
        g_tab7_fg_h = 0u;
        get_sz(SONY_TAB7_BG_NORM, &bg_w, &bg_h);
        get_sz(SONY_TAB7_FG_ICON, &g_tab7_fg_w, &g_tab7_fg_h);
        get_sz(0x020cc117u, &ref_w, &ref_h);
        a7r3_log_hex("SDK_TAB7_BG_SZ=", ((unsigned long)bg_w << 16) | bg_h);
        a7r3_log_hex("SDK_TAB7_FG_SZ=", ((unsigned long)g_tab7_fg_w << 16) | g_tab7_fg_h);
        a7r3_log_hex("SDK_TAB7_REF_SZ=", ((unsigned long)ref_w << 16) | ref_h);
    }

    /* 1. Verify CmnViewSettingNode menu root */
    root = (struct cmn_setting_node *)(vu2_base + VU2_MENU_ROOT_OFF);
    if (!a7r3_pm_writable(root, sizeof(*root)) ||
        root->count != 6 || !root->children ||
        !a7r3_pm_readable(root->children, 6u * sizeof(void *))) {
        a7r3_log_str("SDK_TAB7_BAD_ROOT\n");
        return -2;
    }

    /* 2. Hook TextSprite::SetString(res_id) GOT in libObj.so */
    text_slot = (volatile unsigned long *)(obj_base + LIBOBJ_TEXT_SETSTR_ID_GOT);
    if (!a7r3_pm_writable((const void *)text_slot, 4u)) {
        a7r3_log_str("SDK_TAB7_BAD_TEXT_GOT\n");
        return -3;
    }
    g_orig_text_setstr_got = *text_slot;
    g_real_text_setstr_id = (text_setstr_id_fn)(obj_base + LIBOBJ_TEXT_SETSTR_ID_REAL);
    g_real_text_setstr_cstr = (text_setstr_cstr_fn)(obj_base + LIBOBJ_TEXT_SETSTR_CSTR_REAL);
    a7r3_dsb();
    *text_slot = (unsigned long)(uintptr_t)&tab7_text_setstr_hook;
    a7r3_dsb();
    g_text_setstr_got = text_slot;
    a7r3_log_hex("SDK_TAB7_TEXT_HOOK=", 1UL);

    /* 3. Hook CmnViewProcessDataMgr singleton vptr in viewUnified2.so */
    mgr_guard = (volatile unsigned char *)(vu2_base + VU2_PROC_MGR_GUARD);
    mgr_inst  = (volatile unsigned long *)(vu2_base + VU2_PROC_MGR_INST);
    if (!a7r3_pm_readable((const void *)mgr_guard, 8u) ||
        (*mgr_guard & 1u) == 0u ||
        !a7r3_pm_writable((const void *)mgr_inst, 4u) ||
        *mgr_inst != (vu2_base + VU2_PROC_MGR_VPTR)) {
        a7r3_log_hex("SDK_TAB7_MGR_ERR=", a7r3_pm_readable((const void *)mgr_inst, 4u) ? *mgr_inst : 0UL);
        a7r3_native_tab7_remove();
        return -4;
    }
    orig_mgr_vt = (const unsigned long *)(vu2_base + VU2_PROC_MGR_VPTR - 8UL);
    if (!a7r3_pm_readable(orig_mgr_vt, PROC_MGR_VT_COPY_WORDS * 4u)) {
        a7r3_native_tab7_remove();
        return -4;
    }
    for (i = 0u; i < PROC_MGR_VT_COPY_WORDS; i++)
        g_mgr_vt_raw[i] = orig_mgr_vt[i];

    g_real_get_value      = (mgr_get_value_fn)orig_mgr_vt[2u + 3u];
    g_real_is_grayout     = (mgr_query_fn)    orig_mgr_vt[2u + 9u];
    g_real_is_grayout_val = (mgr_query_val_fn)orig_mgr_vt[2u + 11u];
    g_real_is_disable     = (mgr_query_fn)    orig_mgr_vt[2u + 13u];
    g_real_is_disable_val = (mgr_query_val_fn)orig_mgr_vt[2u + 14u];
    g_real_set_value      = (mgr_set_value_fn)orig_mgr_vt[2u + 17u];
    g_real_exec           = (mgr_exec_fn)     orig_mgr_vt[2u + 21u];

    g_mgr_vt_raw[2u + 3u]  = (unsigned long)(uintptr_t)&mgr_get_value_hook;
    g_mgr_vt_raw[2u + 9u]  = (unsigned long)(uintptr_t)&mgr_is_grayout_hook;
    g_mgr_vt_raw[2u + 11u] = (unsigned long)(uintptr_t)&mgr_is_grayout_val_hook;
    g_mgr_vt_raw[2u + 13u] = (unsigned long)(uintptr_t)&mgr_is_disable_hook;
    g_mgr_vt_raw[2u + 14u] = (unsigned long)(uintptr_t)&mgr_is_disable_val_hook;
    g_mgr_vt_raw[2u + 17u] = (unsigned long)(uintptr_t)&mgr_set_value_hook;
    g_mgr_vt_raw[2u + 21u] = (unsigned long)(uintptr_t)&mgr_exec_hook;

    g_orig_mgr_vptr = *mgr_inst;
    g_mgr_vptr_slot = mgr_inst;
    a7r3_dsb();
    *mgr_inst = (unsigned long)(uintptr_t)&g_mgr_vt_raw[2];
    a7r3_dsb();
    a7r3_log_hex("SDK_TAB7_MGR_HOOK=", 1UL);

    /* 4. Hook PAS_MenuListTab layout updater vtable slot */
    mlt_slot = (volatile unsigned long *)(vu2_base + VU2_MLT_UPDATE_SLOT);
    if (!a7r3_pm_writable((const void *)mlt_slot, 4u) ||
        *mlt_slot != (vu2_base + VU2_MLT_UPDATE_REAL)) {
        a7r3_log_hex("SDK_TAB7_MLT_MISMATCH=", a7r3_pm_readable((const void *)mlt_slot, 4u) ? *mlt_slot : 0UL);
        a7r3_native_tab7_remove();
        return -5;
    }
    g_orig_mlt_update = *mlt_slot;
    g_real_mlt_update = (mlt_update_fn)(vu2_base + VU2_MLT_UPDATE_REAL);
    g_mlt_place_wgt = (mlt_place_wgt_fn)(vu2_base + VU2_MLT_PLACE_WGT_REAL);
    g_mlt_get_row_cnt = (mlt_get_row_cnt_fn)(vu2_base + VU2_MLT_GET_ROW_CNT_REAL);
    a7r3_dsb();
    *mlt_slot = (unsigned long)(uintptr_t)&tab7_mlt_update_hook;
    a7r3_dsb();
    g_mlt_update_slot = mlt_slot;
    a7r3_log_hex("SDK_TAB7_MLT_HOOK=", 1UL);

    /* 5. Build native sub-tree & extend menu root from 6 to 7 tabs */
    build_native_tree_nodes(root->vptr);
    for (i = 0u; i < 6u; i++)
        g_root_children7[i] = root->children[i];
    g_root_children7[6] = &g_tab7_node;

    g_orig_root_children = root->children;
    g_orig_root_count = root->count;
    g_root_node = root;

    a7r3_dsb();
    root->children = g_root_children7;
    a7r3_dsb();
    root->count = 7;
    a7r3_dsb();

    g_tab7_installed = 1;
    a7r3_log_hex("SDK_TAB7_INSTALLED=", 1UL);
    return 0;
}

void a7r3_native_tab7_remove(void)
{
    if (g_root_node && g_orig_root_children) {
        if (g_root_node->selected >= 6)
            g_root_node->selected = 0;
        g_root_node->count = g_orig_root_count;
        a7r3_dsb();
        g_root_node->children = g_orig_root_children;
        a7r3_dsb();
        g_root_node = NULL;
        g_orig_root_children = NULL;
    }

    if (g_mlt_update_slot) {
        *g_mlt_update_slot = g_orig_mlt_update;
        g_mlt_update_slot = NULL;
    }
    if (g_mgr_vptr_slot) {
        *g_mgr_vptr_slot = g_orig_mgr_vptr;
        g_mgr_vptr_slot = NULL;
    }
    if (g_text_setstr_got) {
        *g_text_setstr_got = g_orig_text_setstr_got;
        g_text_setstr_got = NULL;
    }

    a7r3_dsb();
    a7r3_nanosleep_ms(30u);

    if (g_tab7_installed) {
        a7r3_log_hex("SDK_TAB7_RESTORED=", 1UL);
        g_tab7_installed = 0;
    }
}

int a7r3_native_tab7_take_stats(unsigned *out_mlt_hits, unsigned *out_mgr_hits,
                                unsigned *out_text_hits)
{
    unsigned m = g_mlt_hits;
    unsigned g = g_mgr_hits;
    unsigned t = g_text_hits;
    if (m == g_last_mlt_hits && g == g_last_mgr_hits && t == g_last_text_hits)
        return 0;
    g_last_mlt_hits = m;
    g_last_mgr_hits = g;
    g_last_text_hits = t;
    if (out_mlt_hits)
        *out_mlt_hits = m;
    if (out_mgr_hits)
        *out_mgr_hits = g;
    if (out_text_hits)
        *out_text_hits = t;
    return 1;
}
