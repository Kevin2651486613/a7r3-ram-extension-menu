#include "a7r3_pb_focus.h"
#include "a7r3_fb.h"
#include "a7r3_sys.h"

/* Verified A7R3 v3.10 libmpr.so / libObj.so / viewUnified2.so offsets */
#define MPR_FEACORE_PTR_OFF     0x00874b4cUL /* literal holding &(s_FeaCore) */
#define MPR_FEACORE_VPTR_OFF    0x00a0af20UL
#define MPR_FEACORE_SENDCB_REAL 0x0050c675UL /* Thumb, vtable slot +7 (+0x1c) */
#define FEACORE_VT_COPY_WORDS   14u
#define FEACORE_VT_SLOT_SENDCB  7u

#define LIBOBJ_PLAY_SINGLE_GOT  0x0121b818UL
#define MPR_PLAY_SINGLE_REAL    0x00881889UL /* Thumb */

#define VU2_VSPB_SLOT_DTOR1     0x00861990UL /* ViewSinglePb vtable slot[8] */
#define VU2_VSPB_SLOT_DTOR2     0x00861994UL /* ViewSinglePb vtable slot[9] */
#define VU2_VSPB_SLOT_OPEN      0x00861a40UL /* ViewSinglePb vtable slot[52] */
#define VU2_VSPB_SLOT_CLOSE     0x00861a44UL /* ViewSinglePb vtable slot[53] */
#define VU2_VSPB_SLOT_EXIT      0x00861a54UL /* ViewSinglePb vtable slot[57] */
#define VU2_VSPB_DTOR1_REAL     0x001ead39UL /* Thumb */
#define VU2_VSPB_DTOR2_REAL     0x001ead81UL /* Thumb */
#define VU2_VSPB_OPEN_REAL      0x001f5a7bUL /* Thumb */
#define VU2_VSPB_CLOSE_REAL     0x001f5b05UL /* Thumb */
#define VU2_VSPB_EXIT_REAL      0x001f79bdUL /* Thumb */

#define VU2_CUR_PB_BASE_OFF     0x00a6cd2cUL
#define VU2_ENLARGE_OPEN_OFF    0x00a6cd36UL
#define VU2_MOVIE_PB_OFF        0x00a6cd53UL
#define VU2_PB_BASE_MASK        0x000000f0u
#define VU2_PB_BASE_SINGLE      0x00000010u
#define VU2_OVERLAY_HIDE_MASK   (0x00000002u | 0x00004000u | 0x00800000u | 0x08000000u | 0x10000000u)
#define VU2_GET_DISP_TYPE_GOT   0x008cc9d4UL
#define VU2_GET_DISP_TYPE_REAL  0x003cdc45UL /* Thumb */
#define VU2_GROUP_PB_STAT_OFF   0x00a6e1e8UL
#define VU2_CONT_PB_GRP_OFF     0x00a6e1e9UL

#define CB_ID_PLAY_STILL_SINGLE 0x40000022u
#define CB_ID_READ_STILL_SINGLE 0x40000085u
#define EXIF_BUF_SIZE           0x20000u
#define EXIF_RING_SLOTS         4u

typedef int (*feacore_send_cb_fn)(void *self, unsigned cb_id, void *cb_data, unsigned cb_len);
typedef void *(*vspb_dtor_fn)(void *self);
typedef int (*vspb_open_close_fn)(void *self, unsigned a1, unsigned a2, unsigned a3);
typedef int (*vspb_exit_fn)(void *self);
typedef int (*get_disp_type_fn)(void);
typedef int (*play_still_single_fn)(void *self, unsigned long media,
                                    unsigned e0, unsigned e1, unsigned e2, unsigned e3,
                                    unsigned long opt, void *ctrl, void *exif_buf,
                                    unsigned flag, void *raw_info);

struct mpr_buffer_area {
    unsigned long virt_addr;
    unsigned size;
    unsigned rsv0;
    unsigned rsv1;
};

static uint8_t g_exif_ring[EXIF_RING_SLOTS][EXIF_BUF_SIZE] __attribute__((aligned(16)));
static struct mpr_buffer_area g_exif_areas[EXIF_RING_SLOTS];
static volatile unsigned g_exif_slot_idx;

static unsigned long g_mpr_base;
static unsigned long g_obj_base;
static unsigned long g_vu2_base;

static unsigned long g_feacore_vt_raw[16];
static volatile unsigned long *g_feacore_vptr_slot;
static unsigned long g_orig_feacore_vptr;
static feacore_send_cb_fn g_real_send_cb;

static volatile unsigned long *g_vspb_open_slot;
static volatile unsigned long *g_vspb_close_slot;
static volatile unsigned long *g_vspb_exit_slot;
static volatile unsigned long *g_vspb_dtor1_slot;
static volatile unsigned long *g_vspb_dtor2_slot;
static unsigned long g_orig_vspb_open;
static unsigned long g_orig_vspb_close;
static unsigned long g_orig_vspb_exit;
static unsigned long g_orig_vspb_dtor1;
static unsigned long g_orig_vspb_dtor2;
static vspb_open_close_fn g_real_vspb_open;
static vspb_open_close_fn g_real_vspb_close;
static vspb_exit_fn g_real_vspb_exit;
static vspb_dtor_fn g_real_vspb_dtor1;
static vspb_dtor_fn g_real_vspb_dtor2;

static volatile unsigned long *g_play_single_got;
static unsigned long g_orig_play_single_got;
static play_still_single_fn g_real_play_single;

static volatile unsigned long *g_get_disp_type_got;
static unsigned long g_orig_get_disp_type_got;
static get_disp_type_fn g_real_get_disp_type;

static volatile int g_pb_installed;
static volatile int g_pb_enabled;
static volatile int g_in_still_pb;
static volatile int g_pb_disp_mode;
static volatile int g_pb_group_type;
static int g_last_logged_mode = -1;
static int g_last_logged_grp = -1;
static unsigned g_last_logged_stat = ~0u;
static volatile unsigned g_vspb_evt;
static volatile unsigned g_vspb_seq;
static unsigned g_last_vspb_seq;

static volatile unsigned g_cb_seq;
static unsigned g_last_cb_seq;
static volatile unsigned g_last_cb_id;
static volatile unsigned g_last_cb_len;
static volatile unsigned g_last_err_cd;
static volatile unsigned g_last_main_w;
static volatile unsigned g_last_main_h;
static volatile unsigned g_last_main_rot;
static volatile unsigned g_last_err_exif;
static volatile unsigned g_last_exif_sz;
static volatile unsigned g_last_exif_hdr;

static volatile unsigned g_parse_stage;
static volatile unsigned g_floc_w;
static volatile unsigned g_floc_h;
static volatile unsigned g_floc_x;
static volatile unsigned g_floc_y;
static volatile unsigned g_exif_orient;
static volatile int g_focus_valid;
static volatile int g_osd_cx;
static volatile int g_osd_cy;

static volatile unsigned g_other_cb_id;
static volatile unsigned g_other_cb_len;
static volatile unsigned g_other_cb_seq;
static unsigned g_last_other_seq;

static unsigned rd16(const uint8_t *p, int big_endian)
{
    if (big_endian)
        return ((unsigned)p[0] << 8) | (unsigned)p[1];
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd32(const uint8_t *p, int big_endian)
{
    if (big_endian)
        return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
               ((unsigned)p[2] << 8)  | (unsigned)p[3];
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int range_ok(unsigned off, unsigned need, unsigned total)
{
    return (off <= total) && (need <= total - off);
}

static unsigned find_tiff_header(const uint8_t *buf, unsigned len, int *out_be)
{
    unsigned limit = (len < 512u) ? len : 512u;
    unsigned i;

    if (len < 8u)
        return ~0u;
    for (i = 0u; i + 8u <= limit; i++) {
        if (buf[i] == 'I' && buf[i + 1u] == 'I' &&
            buf[i + 2u] == 0x2au && buf[i + 3u] == 0x00u) {
            *out_be = 0;
            return i;
        }
        if (buf[i] == 'M' && buf[i + 1u] == 'M' &&
            buf[i + 2u] == 0x00u && buf[i + 3u] == 0x2au) {
            *out_be = 1;
            return i;
        }
    }
    return ~0u;
}

static int parse_sony_focus_exif(const uint8_t *buf, unsigned len,
                                 unsigned *out_w, unsigned *out_h,
                                 unsigned *out_x, unsigned *out_y,
                                 unsigned *out_orient)
{
    int be = 0;
    unsigned tiff_off, ifd0_rel, n0, i;
    unsigned orient = 1u;
    unsigned exif_ifd_rel = 0u;
    unsigned mn_rel = 0u, mn_len = 0u;
    unsigned mn_abs, mn_ifd, mn_cnt, tiff_avail;

    if (!buf || len < 16u)
        return 0;

    tiff_off = find_tiff_header(buf, len, &be);
    if (tiff_off == ~0u || !range_ok(tiff_off, 8u, len))
        return 1;

    tiff_avail = len - tiff_off;
    ifd0_rel = rd32(buf + tiff_off + 4u, be);
    if (!range_ok(ifd0_rel, 2u, tiff_avail))
        return 1;

    n0 = rd16(buf + tiff_off + ifd0_rel, be);
    if (n0 > 64u)
        n0 = 64u;
    for (i = 0u; i < n0; i++) {
        unsigned eoff = ifd0_rel + 2u + i * 12u;
        unsigned tag;
        if (!range_ok(eoff, 12u, tiff_avail))
            break;
        tag = rd16(buf + tiff_off + eoff, be);
        if (tag == 0x0112u) {
            orient = rd16(buf + tiff_off + eoff + 8u, be);
        } else if (tag == 0x8769u) {
            exif_ifd_rel = rd32(buf + tiff_off + eoff + 8u, be);
        }
    }
    if (out_orient)
        *out_orient = orient;
    if (exif_ifd_rel == 0u || !range_ok(exif_ifd_rel, 2u, tiff_avail))
        return 2;

    n0 = rd16(buf + tiff_off + exif_ifd_rel, be);
    if (n0 > 128u)
        n0 = 128u;
    for (i = 0u; i < n0; i++) {
        unsigned eoff = exif_ifd_rel + 2u + i * 12u;
        unsigned tag;
        if (!range_ok(eoff, 12u, tiff_avail))
            break;
        tag = rd16(buf + tiff_off + eoff, be);
        if (tag == 0x927cu) {
            mn_len = rd32(buf + tiff_off + eoff + 4u, be);
            mn_rel = rd32(buf + tiff_off + eoff + 8u, be);
            break;
        }
    }
    if (mn_rel == 0u || !range_ok(mn_rel, 14u, tiff_avail))
        return 3;

    mn_abs = tiff_off + mn_rel;
    if (mn_len < 16u || !range_ok(mn_abs, mn_len, len))
        mn_len = len - mn_abs;

    /* Sony MakerNote typically begins with "SONY DSC \0\0\0" (12 bytes) */
    if (mn_len >= 14u &&
        buf[mn_abs + 0u] == 'S' && buf[mn_abs + 1u] == 'O' &&
        buf[mn_abs + 2u] == 'N' && buf[mn_abs + 3u] == 'Y') {
        mn_ifd = mn_abs + 12u;
    } else {
        mn_ifd = mn_abs;
    }
    if (!range_ok(mn_ifd, 2u, len))
        return 3;

    mn_cnt = rd16(buf + mn_ifd, be);
    if (mn_cnt == 0u || mn_cnt > 256u) {
        if (range_ok(mn_abs, 14u, len)) {
            mn_ifd = mn_abs + 12u;
            mn_cnt = rd16(buf + mn_ifd, be);
        }
    }
    if (mn_cnt == 0u || mn_cnt > 256u)
        return 3;

    for (i = 0u; i < mn_cnt; i++) {
        unsigned eoff = mn_ifd + 2u + i * 12u;
        unsigned tag, typ, cnt, val_rel, voff, elem_sz, need_bytes;
        unsigned fw, fh, fx, fy;
        if (!range_ok(eoff, 12u, len))
            break;
        tag = rd16(buf + eoff, be);
        if (tag != 0x2027u)
            continue;
        typ = rd16(buf + eoff + 2u, be);
        cnt = rd32(buf + eoff + 4u, be);
        val_rel = rd32(buf + eoff + 8u, be);
        if (cnt < 4u || (typ != 3u && typ != 4u))
            return 4;
        elem_sz = (typ == 3u) ? 2u : 4u;
        need_bytes = 4u * elem_sz;
        if (range_ok(val_rel, need_bytes, tiff_avail))
            voff = tiff_off + val_rel;
        else if (range_ok(val_rel, need_bytes, len - mn_abs))
            voff = mn_abs + val_rel;
        else
            return 4;

        if (typ == 3u) {
            fw = rd16(buf + voff + 0u, be);
            fh = rd16(buf + voff + 2u, be);
            fx = rd16(buf + voff + 4u, be);
            fy = rd16(buf + voff + 6u, be);
        } else {
            fw = rd32(buf + voff + 0u, be);
            fh = rd32(buf + voff + 4u, be);
            fx = rd32(buf + voff + 8u, be);
            fy = rd32(buf + voff + 12u, be);
        }
        if (fw == 0u || fh == 0u || fw > 32768u || fh > 32768u || fx >= fw || fy >= fh)
            return 4;
        *out_w = fw;
        *out_h = fh;
        *out_x = fx;
        *out_y = fy;
        return 5;
    }
    return 4;
}

static unsigned udiv32(unsigned num, unsigned den)
{
    unsigned q = 0u, r = 0u;
    int b;
    if (den == 0u)
        return 0u;
    for (b = 31; b >= 0; b--) {
        r = (r << 1) | ((num >> (unsigned)b) & 1u);
        if (r >= den) {
            r -= den;
            q |= (1u << (unsigned)b);
        }
    }
    return q;
}

static void compute_osd_coords(unsigned iw, unsigned ih,
                               unsigned fx, unsigned fy,
                               unsigned orient, unsigned main_rot,
                               int *out_cx, int *out_cy)
{
    unsigned eff = 1u;
    unsigned rx, ry, rw, rh;
    unsigned disp_w, disp_h, off_x, off_y;

    if (orient == 1u || orient == 3u || orient == 6u || orient == 8u) {
        eff = orient;
    } else if (main_rot == 1u || main_rot == 90u) {
        eff = 6u;
    } else if (main_rot == 2u || main_rot == 180u) {
        eff = 3u;
    } else if (main_rot == 3u || main_rot == 270u) {
        eff = 8u;
    }

    switch (eff) {
    case 6u: /* 90 deg CW */
        rx = (ih - 1u) - fy;
        ry = fx;
        rw = ih;
        rh = iw;
        break;
    case 8u: /* 270 deg CW */
        rx = fy;
        ry = (iw - 1u) - fx;
        rw = ih;
        rh = iw;
        break;
    case 3u: /* 180 deg */
        rx = (iw - 1u) - fx;
        ry = (ih - 1u) - fy;
        rw = iw;
        rh = ih;
        break;
    default:
        rx = fx;
        ry = fy;
        rw = iw;
        rh = ih;
        break;
    }

    if (rw * A7R3_FB_H >= rh * A7R3_FB_W) {
        disp_w = A7R3_FB_W;
        disp_h = udiv32(A7R3_FB_W * rh, rw);
        if (disp_h == 0u)
            disp_h = 1u;
        if (disp_h > A7R3_FB_H)
            disp_h = A7R3_FB_H;
        off_x = 0u;
        off_y = (A7R3_FB_H - disp_h) >> 1;
    } else {
        disp_h = A7R3_FB_H;
        disp_w = udiv32(A7R3_FB_H * rw, rh);
        if (disp_w == 0u)
            disp_w = 1u;
        if (disp_w > A7R3_FB_W)
            disp_w = A7R3_FB_W;
        off_x = (A7R3_FB_W - disp_w) >> 1;
        off_y = 0u;
    }

    {
        int cx = (int)(off_x + udiv32(rx * disp_w, rw));
        int cy = (int)(off_y + udiv32(ry * disp_h, rh));
        if (cx < 0)
            cx = 0;
        else if (cx >= (int)A7R3_FB_W)
            cx = (int)A7R3_FB_W - 1;
        if (cy < 0)
            cy = 0;
        else if (cy >= (int)A7R3_FB_H)
            cy = (int)A7R3_FB_H - 1;
        *out_cx = cx;
        *out_cy = cy;
    }
}

__attribute__((noinline))
static int feacore_send_cb_hook(void *self, unsigned cb_id, void *cb_data, unsigned cb_len)
{
    if (cb_id == CB_ID_PLAY_STILL_SINGLE && cb_data != NULL && cb_len >= 0x40u) {
        unsigned *w = (unsigned *)cb_data;
        unsigned err_cd   = w[0];
        unsigned main_w   = w[2];
        unsigned main_h   = w[3];
        unsigned main_rot = w[4];
        unsigned err_exif = w[8];
        unsigned exif_va  = w[9];
        unsigned exif_sz  = w[10];
        int is_ours = 0;
        int no_exif_req;
        unsigned fw = 0u, fh = 0u, fx = 0u, fy = 0u, orient = 1u;
        unsigned s;
        int stage = 0;

        for (s = 0u; s < EXIF_RING_SLOTS; s++) {
            if (exif_va == (unsigned long)(uintptr_t)g_exif_ring[s]) {
                is_ours = 1;
                break;
            }
        }
        no_exif_req = (!is_ours && err_exif == 0x2au && exif_va == 0u && exif_sz == 0u);

        g_last_cb_id   = cb_id;
        g_last_cb_len  = cb_len;
        g_last_err_cd  = err_cd;
        g_last_main_w  = main_w;
        g_last_main_h  = main_h;
        g_last_main_rot = main_rot;
        g_last_err_exif = err_exif;
        g_last_exif_sz  = exif_sz;
        g_last_exif_hdr = 0u;

        if (err_cd == 0u && err_exif == 0u && exif_va > 0x10000UL &&
            exif_sz >= 16u && exif_sz <= (4u * 1024u * 1024u)) {
            const uint8_t *ebuf = (const uint8_t *)(uintptr_t)exif_va;
            g_last_exif_hdr = rd32(ebuf, 1);
            stage = parse_sony_focus_exif(ebuf, exif_sz, &fw, &fh, &fx, &fy, &orient);
        }

        if (is_ours) {
            /* Always restore stock no-EXIF fields before libObj sees the callback. */
            w[8] = 0x2au;
            w[9] = 0u;
            w[10] = 0u;
            a7r3_dsb();
        }

        g_parse_stage = (unsigned)stage;
        g_floc_w = fw;
        g_floc_h = fh;
        g_floc_x = fx;
        g_floc_y = fy;
        g_exif_orient = orient;

        if (err_cd == 0u) {
            if (stage == 5) {
                int cx = 320, cy = 240;
                compute_osd_coords(fw, fh, fx, fy, orient, main_rot, &cx, &cy);
                g_osd_cx = cx;
                g_osd_cy = cy;
                g_focus_valid = 1;
            } else if (!no_exif_req) {
                g_focus_valid = 0;
            }
        }
        a7r3_dsb();
        g_cb_seq++;
        a7r3_dsb();
    } else if (cb_id == CB_ID_READ_STILL_SINGLE || (cb_id & 0xff000000u) == 0x40000000u) {
        g_other_cb_id = cb_id;
        g_other_cb_len = cb_len;
        a7r3_dsb();
        g_other_cb_seq++;
    }

    return g_real_send_cb(self, cb_id, cb_data, cb_len);
}

__attribute__((noinline))
static int play_still_single_hook(void *self, unsigned long media,
                                  unsigned e0, unsigned e1, unsigned e2, unsigned e3,
                                  unsigned long opt, void *ctrl, void *exif_buf,
                                  unsigned flag, void *raw_info)
{
    g_in_still_pb = 1;
    if (g_pb_enabled && exif_buf == NULL) {
        unsigned slot = g_exif_slot_idx;
        g_exif_slot_idx = slot + 1u;
        exif_buf = (void *)&g_exif_areas[slot & (EXIF_RING_SLOTS - 1u)];
    }
    a7r3_dsb();
    return g_real_play_single(self, media, e0, e1, e2, e3,
                              opt, ctrl, exif_buf, flag, raw_info);
}

void a7r3_pb_focus_on_disp_mode(int mode)
{
    g_pb_disp_mode = mode;
    a7r3_dsb();
}

__attribute__((noinline))
static int pb_get_disp_type_hook(void)
{
    int t = g_real_get_disp_type();
    g_pb_group_type = t;
    a7r3_dsb();
    return t;
}

__attribute__((noinline))
static int vspb_open_hook(void *self, unsigned a1, unsigned a2, unsigned a3)
{
    int rc = g_real_vspb_open(self, a1, a2, a3);
    g_in_still_pb = 1;
    if (self != NULL && a7r3_pm_readable((const char *)self + 0x118, 4u))
        g_pb_disp_mode = *(const int *)((const char *)self + 0x118);
    g_vspb_evt = 1u;
    a7r3_dsb();
    g_vspb_seq++;
    return rc;
}

__attribute__((noinline))
static int vspb_close_hook(void *self, unsigned a1, unsigned a2, unsigned a3)
{
    g_in_still_pb = 0;
    g_focus_valid = 0;
    g_pb_group_type = 0;
    g_vspb_evt = 2u;
    a7r3_dsb();
    g_vspb_seq++;
    return g_real_vspb_close(self, a1, a2, a3);
}

__attribute__((noinline))
static int vspb_exit_hook(void *self)
{
    g_in_still_pb = 0;
    g_focus_valid = 0;
    g_pb_group_type = 0;
    g_vspb_evt = 5u;
    a7r3_dsb();
    g_vspb_seq++;
    return g_real_vspb_exit(self);
}

__attribute__((noinline))
static void *vspb_dtor1_hook(void *self)
{
    g_in_still_pb = 0;
    g_focus_valid = 0;
    g_pb_group_type = 0;
    g_vspb_evt = 3u;
    a7r3_dsb();
    g_vspb_seq++;
    return g_real_vspb_dtor1(self);
}

__attribute__((noinline))
static void *vspb_dtor2_hook(void *self)
{
    g_in_still_pb = 0;
    g_focus_valid = 0;
    g_pb_group_type = 0;
    g_vspb_evt = 4u;
    a7r3_dsb();
    g_vspb_seq++;
    return g_real_vspb_dtor2(self);
}

int a7r3_pb_focus_install(void)
{
    unsigned long mpr_base, obj_base, vu2_base;
    const unsigned long *s_core_slot;
    void *core_inst;
    volatile unsigned long *core_vptr_slot;
    const unsigned long *orig_vt;
    volatile unsigned long *play_slot;
    volatile unsigned long *open_slot, *close_slot, *exit_slot, *dtor1_slot, *dtor2_slot;
    unsigned i;

    if (g_pb_installed)
        return 0;

    mpr_base = a7r3_mod_base("/libmpr.so");
    obj_base = a7r3_mod_base("/libObj.so");
    vu2_base = a7r3_mod_base("/viewUnified2.so");
    if (!mpr_base || !obj_base || !vu2_base) {
        a7r3_log_str("SDK_PB_NO_MOD\n");
        return -1;
    }

    s_core_slot = (const unsigned long *)(mpr_base + MPR_FEACORE_PTR_OFF);
    if (!a7r3_pm_readable(s_core_slot, 4u)) {
        a7r3_log_str("SDK_PB_BAD_CORE_LIT\n");
        return -2;
    }
    {
        const unsigned long *pp = (const unsigned long *)(uintptr_t)(*s_core_slot);
        if (!a7r3_pm_readable(pp, 4u)) {
            a7r3_log_str("SDK_PB_BAD_CORE_PTR\n");
            return -2;
        }
        core_inst = (void *)(uintptr_t)(*pp);
    }
    if (!core_inst) {
        a7r3_log_str("SDK_PB_NULL_CORE\n");
        return -2;
    }
    core_vptr_slot = (volatile unsigned long *)core_inst;
    if (*core_vptr_slot != (mpr_base + MPR_FEACORE_VPTR_OFF)) {
        a7r3_log_hex("SDK_PB_VPTR_MISMATCH=", *core_vptr_slot);
        return -3;
    }

    orig_vt = (const unsigned long *)(mpr_base + MPR_FEACORE_VPTR_OFF - 8UL);
    if (!a7r3_pm_readable(orig_vt, FEACORE_VT_COPY_WORDS * 4u)) {
        a7r3_log_str("SDK_PB_VT_UNREADABLE\n");
        return -3;
    }

    play_slot  = (volatile unsigned long *)(obj_base + LIBOBJ_PLAY_SINGLE_GOT);
    open_slot  = (volatile unsigned long *)(vu2_base + VU2_VSPB_SLOT_OPEN);
    close_slot = (volatile unsigned long *)(vu2_base + VU2_VSPB_SLOT_CLOSE);
    exit_slot  = (volatile unsigned long *)(vu2_base + VU2_VSPB_SLOT_EXIT);
    dtor1_slot = (volatile unsigned long *)(vu2_base + VU2_VSPB_SLOT_DTOR1);
    dtor2_slot = (volatile unsigned long *)(vu2_base + VU2_VSPB_SLOT_DTOR2);
    volatile unsigned long *disp_type_slot = (volatile unsigned long *)(vu2_base + VU2_GET_DISP_TYPE_GOT);
    if (!a7r3_pm_writable((const void *)play_slot, 4u)  ||
        !a7r3_pm_writable((const void *)open_slot, 4u)  ||
        !a7r3_pm_writable((const void *)close_slot, 4u) ||
        !a7r3_pm_writable((const void *)exit_slot, 4u)  ||
        !a7r3_pm_writable((const void *)dtor1_slot, 4u) ||
        !a7r3_pm_writable((const void *)dtor2_slot, 4u) ||
        !a7r3_pm_writable((const void *)disp_type_slot, 4u)) {
        a7r3_log_str("SDK_PB_BAD_GOT\n");
        return -4;
    }

    if (*open_slot  != (vu2_base + VU2_VSPB_OPEN_REAL)  ||
        *close_slot != (vu2_base + VU2_VSPB_CLOSE_REAL) ||
        *exit_slot  != (vu2_base + VU2_VSPB_EXIT_REAL)  ||
        *dtor1_slot != (vu2_base + VU2_VSPB_DTOR1_REAL) ||
        *dtor2_slot != (vu2_base + VU2_VSPB_DTOR2_REAL)) {
        a7r3_log_hex("SDK_PB_VSPB_MISMATCH=", *open_slot);
        return -5;
    }

    if (!a7r3_pm_readable((const void *)(vu2_base + VU2_CUR_PB_BASE_OFF), 0x30u) ||
        !a7r3_pm_readable((const void *)(vu2_base + VU2_GROUP_PB_STAT_OFF), 4u)) {
        a7r3_log_str("SDK_PB_BAD_VU2_OFF\n");
        return -4;
    }

    for (i = 0u; i < EXIF_RING_SLOTS; i++) {
        g_exif_areas[i].virt_addr = (unsigned long)(uintptr_t)g_exif_ring[i];
        g_exif_areas[i].size = EXIF_BUF_SIZE;
        g_exif_areas[i].rsv0 = 0u;
        g_exif_areas[i].rsv1 = 0u;
    }

    g_mpr_base = mpr_base;
    g_obj_base = obj_base;
    g_vu2_base = vu2_base;

    for (i = 0u; i < FEACORE_VT_COPY_WORDS; i++)
        g_feacore_vt_raw[i] = orig_vt[i];
    g_real_send_cb = (feacore_send_cb_fn)(mpr_base + MPR_FEACORE_SENDCB_REAL);
    g_feacore_vt_raw[2u + FEACORE_VT_SLOT_SENDCB] = (unsigned long)(uintptr_t)&feacore_send_cb_hook;

    g_orig_feacore_vptr = *core_vptr_slot;
    g_feacore_vptr_slot = core_vptr_slot;

    g_orig_play_single_got = *play_slot;
    g_real_play_single = (play_still_single_fn)(mpr_base + MPR_PLAY_SINGLE_REAL);

    g_orig_vspb_open  = *open_slot;
    g_orig_vspb_close = *close_slot;
    g_orig_vspb_exit  = *exit_slot;
    g_orig_vspb_dtor1 = *dtor1_slot;
    g_orig_vspb_dtor2 = *dtor2_slot;
    g_orig_get_disp_type_got = *disp_type_slot;
    g_real_vspb_open  = (vspb_open_close_fn)(vu2_base + VU2_VSPB_OPEN_REAL);
    g_real_vspb_close = (vspb_open_close_fn)(vu2_base + VU2_VSPB_CLOSE_REAL);
    g_real_vspb_exit  = (vspb_exit_fn)(vu2_base + VU2_VSPB_EXIT_REAL);
    g_real_vspb_dtor1 = (vspb_dtor_fn)(vu2_base + VU2_VSPB_DTOR1_REAL);
    g_real_vspb_dtor2 = (vspb_dtor_fn)(vu2_base + VU2_VSPB_DTOR2_REAL);
    g_real_get_disp_type = (get_disp_type_fn)(vu2_base + VU2_GET_DISP_TYPE_REAL);

    a7r3_dsb();
    *core_vptr_slot = (unsigned long)(uintptr_t)&g_feacore_vt_raw[2];
    *play_slot      = (unsigned long)(uintptr_t)&play_still_single_hook;
    *open_slot      = (unsigned long)(uintptr_t)&vspb_open_hook;
    *close_slot     = (unsigned long)(uintptr_t)&vspb_close_hook;
    *exit_slot      = (unsigned long)(uintptr_t)&vspb_exit_hook;
    *dtor1_slot     = (unsigned long)(uintptr_t)&vspb_dtor1_hook;
    *dtor2_slot     = (unsigned long)(uintptr_t)&vspb_dtor2_hook;
    *disp_type_slot = (unsigned long)(uintptr_t)&pb_get_disp_type_hook;
    a7r3_dsb();

    g_play_single_got   = play_slot;
    g_vspb_open_slot    = open_slot;
    g_vspb_close_slot   = close_slot;
    g_vspb_exit_slot    = exit_slot;
    g_vspb_dtor1_slot   = dtor1_slot;
    g_vspb_dtor2_slot   = dtor2_slot;
    g_get_disp_type_got = disp_type_slot;
    g_pb_installed = 1;
    a7r3_dsb();

    a7r3_log_hex("SDK_PB_HOOKS=", 1UL);
    return 0;
}

void a7r3_pb_focus_remove(void)
{
    if (!g_pb_installed)
        return;

    g_pb_enabled = 0;
    g_in_still_pb = 0;
    g_focus_valid = 0;
    a7r3_dsb();
    a7r3_fb_pb_box_hide();

    /* Stop injecting g_exif_buf first and drain before unhooking FeaCore vptr. */
    if (g_play_single_got) {
        *g_play_single_got = g_orig_play_single_got;
        g_play_single_got = NULL;
    }
    a7r3_dsb();
    a7r3_nanosleep_ms(30u);

    if (g_feacore_vptr_slot) {
        *g_feacore_vptr_slot = g_orig_feacore_vptr;
        g_feacore_vptr_slot = NULL;
    }
    if (g_vspb_open_slot) {
        *g_vspb_open_slot = g_orig_vspb_open;
        g_vspb_open_slot = NULL;
    }
    if (g_vspb_close_slot) {
        *g_vspb_close_slot = g_orig_vspb_close;
        g_vspb_close_slot = NULL;
    }
    if (g_vspb_exit_slot) {
        *g_vspb_exit_slot = g_orig_vspb_exit;
        g_vspb_exit_slot = NULL;
    }
    if (g_vspb_dtor1_slot) {
        *g_vspb_dtor1_slot = g_orig_vspb_dtor1;
        g_vspb_dtor1_slot = NULL;
    }
    if (g_vspb_dtor2_slot) {
        *g_vspb_dtor2_slot = g_orig_vspb_dtor2;
        g_vspb_dtor2_slot = NULL;
    }
    if (g_get_disp_type_got) {
        *g_get_disp_type_got = g_orig_get_disp_type_got;
        g_get_disp_type_got = NULL;
    }
    a7r3_dsb();
    a7r3_nanosleep_ms(50u);
    g_pb_installed = 0;
    a7r3_log_hex("SDK_PB_RESTORED=", 1UL);
}

void a7r3_pb_focus_set_enabled(int enable)
{
    int on = enable ? 1 : 0;
    if (on == g_pb_enabled)
        return;
    g_pb_enabled = on;
    a7r3_dsb();
    if (!on)
        a7r3_fb_pb_box_hide();
    a7r3_log_hex("SDK_PB_FOCUS=", (unsigned long)on);
}

int a7r3_pb_focus_get_enabled(void)
{
    return g_pb_enabled;
}

int a7r3_pb_focus_allow_stamp(void)
{
    unsigned st;
    uint8_t enl, mov;
    if (!g_pb_installed || !g_pb_enabled || !g_in_still_pb || !g_focus_valid || !g_vu2_base)
        return 0;
    st  = *(const volatile unsigned *)(g_vu2_base + VU2_CUR_PB_BASE_OFF);
    enl = *(const volatile uint8_t *)(g_vu2_base + VU2_ENLARGE_OPEN_OFF);
    mov = *(const volatile uint8_t *)(g_vu2_base + VU2_MOVIE_PB_OFF);
    if ((st & VU2_PB_BASE_MASK) != VU2_PB_BASE_SINGLE)
        return 0;
    if ((st & VU2_OVERLAY_HIDE_MASK) != 0u)
        return 0;
    if (enl != 0u || mov != 0u)
        return 0;
    if (g_pb_disp_mode == 2 || g_pb_group_type == 2)
        return 0;
    return 1;
}

void a7r3_pb_focus_tick(int md3_open)
{
    unsigned cur_pb_base = 0u;
    unsigned enlarge_open = 0u;
    uint8_t grp_pb = 0u;
    uint8_t cont_grp = 0u;
    int disp_mode, grp_code;
    int single_pb_top, want;

    if (!g_pb_installed)
        return;

    if (g_vu2_base) {
        cur_pb_base = *(const volatile unsigned *)(g_vu2_base + VU2_CUR_PB_BASE_OFF);
        enlarge_open = *(const volatile uint8_t *)(g_vu2_base + VU2_ENLARGE_OPEN_OFF);
        grp_pb = *(const volatile uint8_t *)(g_vu2_base + VU2_GROUP_PB_STAT_OFF);
        cont_grp = *(const volatile uint8_t *)(g_vu2_base + VU2_CONT_PB_GRP_OFF);
    }

    if (g_cb_seq != g_last_cb_seq) {
        g_last_cb_seq = g_cb_seq;
        a7r3_log_hex("SDK_PB_CB=", ((unsigned long)g_last_err_cd << 16) | (g_last_err_exif & 0xffffu));
        a7r3_log_hex("SDK_PB_PARSE=",
                     (((unsigned long)g_parse_stage & 0xffu) << 24) |
                     (((unsigned long)g_exif_orient & 0xffu) << 16) |
                     ((unsigned long)(cur_pb_base ? 1u : 0u) << 8) |
                     ((unsigned long)(enlarge_open ? 1u : 0u)));
        a7r3_log_hex("SDK_PB_BOX_XY=",
                     (((unsigned long)(unsigned)g_osd_cx & 0xffffu) << 16) |
                     ((unsigned long)(unsigned)g_osd_cy & 0xffffu));
    }

    if (g_other_cb_seq != g_last_other_seq) {
        g_last_other_seq = g_other_cb_seq;
        if (g_pb_enabled)
            a7r3_log_hex("SDK_PB_OTHER_CB=", (unsigned long)g_other_cb_id);
    }

    if (g_vspb_seq != g_last_vspb_seq) {
        g_last_vspb_seq = g_vspb_seq;
        a7r3_log_hex("SDK_PB_VSPB=", (unsigned long)g_vspb_evt);
    }

    disp_mode = g_pb_disp_mode;
    grp_code = (int)((((unsigned)g_pb_group_type & 0xffffu) << 16) |
                     (((unsigned)grp_pb & 0xffu) << 8) |
                     ((unsigned)cont_grp & 0xffu));
    if (g_pb_enabled && cur_pb_base != g_last_logged_stat) {
        g_last_logged_stat = cur_pb_base;
        a7r3_log_hex("SDK_PB_STAT=", (unsigned long)cur_pb_base);
    }
    if (g_in_still_pb && g_pb_enabled) {
        if (disp_mode != g_last_logged_mode) {
            g_last_logged_mode = disp_mode;
            a7r3_log_hex("SDK_PB_DISP_MODE=", (unsigned long)(unsigned)disp_mode);
        }
        if (grp_code != g_last_logged_grp) {
            g_last_logged_grp = grp_code;
            a7r3_log_hex("SDK_PB_GRP=", (unsigned long)(unsigned)grp_code);
        }
    }

    single_pb_top = ((cur_pb_base & VU2_PB_BASE_MASK) == VU2_PB_BASE_SINGLE);
    if (!single_pb_top && g_focus_valid)
        g_focus_valid = 0;

    want = !md3_open && a7r3_pb_focus_allow_stamp();
    if (want)
        a7r3_fb_pb_box_show(g_osd_cx, g_osd_cy, 18, 18);
    else
        a7r3_fb_pb_box_hide();
}
