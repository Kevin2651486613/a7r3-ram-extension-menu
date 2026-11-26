#include "a7r3_dual_hud.h"
#include "a7r3_pb_focus.h"
#include "a7r3_sys.h"

#define DUAL_HUD_BIN_PATH       "/tmp_bt/dual_hud_payload.bin"
#define DUAL_HUD_BIN_MAGIC      0x44554844u /* "DHUD" little-endian */
#define DUAL_HUD_PAYLOAD_LEN    2380u

/* Verified A7R3 v3.10 libObj.so & viewUnified2.so GOT / vtable offsets */
#define LIBOBJ_GET_RESID_GOT    0x0121a5c0UL
#define LIBOBJ_REC_DTOR_GOT     0x0121a51cUL
#define LIBOBJ_GET_RESID_REAL   0x00798d31UL /* Thumb */
#define LIBOBJ_REC_DTOR_REAL    0x00798d23UL /* Thumb */
#define LIBOBJ_PARSER_CALL_RET  0x0059fecaUL /* ResourceLayoutParser: blx getResID */

#define VU2_GET_LAYOUT_MODE_GOT 0x008cd0fcUL
#define VU2_GET_LAYOUT_REAL     0x0029c411UL /* Thumb */
#define VU2_LEVEL_ROUTE_SLOT    0x008602d4UL
#define VU2_LEVEL_ROUTE_REAL    0x0017a0b1UL /* Thumb */
#define VU2_GATE1_CALL_RET      0x001ae78cUL /* Gate 1: blx UserGetLayoutMode */

#define RES_ID_LEVEL_OUTER      0x02008029u
#define WID_HISTOGRAM           0xc96d8ddcu
#define WID_NEW_MASTER          0xd1153927u
#define WID_ROOT_MASTER         0x984516c0u
#define SAVED_REC_SLOTS         8u

typedef unsigned (*get_res_id_fn)(void *rec_ptr);
typedef void *(*rec_dtor_fn)(void *rec_ptr);
typedef int (*get_layout_mode_fn)(void *self, int dev);
typedef int (*level_route_fn)(void *self, unsigned widget_id, unsigned *out_master_id);

struct saved_rec_slot {
    void *volatile addr;
    void *orig_w0;
};

/* Populated once at install time from /tmp_bt/dual_hud_payload.bin (zero runtime I/O). */
static struct {
    uint32_t tag; /* Bit 31 clear (0x00000000u) -> inline RecordPtr data at +4 */
    uint8_t payload[DUAL_HUD_PAYLOAD_LEN];
} __attribute__((aligned(4))) g_dual_level_rec;

static struct saved_rec_slot g_saved_recs[SAVED_REC_SLOTS];

static unsigned long g_obj_base;
static unsigned long g_vu2_base;

static volatile unsigned long *g_get_resid_got;
static volatile unsigned long *g_rec_dtor_got;
static volatile unsigned long *g_get_layout_got;
static volatile unsigned long *g_route_slot;

static unsigned long g_orig_get_resid_got;
static unsigned long g_orig_rec_dtor_got;
static unsigned long g_orig_get_layout_got;
static unsigned long g_orig_route_slot;

static get_res_id_fn g_real_get_res_id;
static rec_dtor_fn g_real_rec_dtor;
static get_layout_mode_fn g_real_get_layout_mode;
static level_route_fn g_real_level_route;

static volatile int g_dual_has_payload;
static volatile int g_dual_installed;
static volatile int g_dual_enabled;
static volatile int g_dual_ever_parsed;

static volatile unsigned g_parse_hits;
static volatile unsigned g_gate1_hits;
static volatile unsigned g_route_hits;

static unsigned g_last_parse_hits;
static unsigned g_last_gate1_hits;
static unsigned g_last_route_hits;

static uint32_t dual_fnv1a32(const uint8_t *buf, unsigned len)
{
    uint32_t h = 2166136261u;
    unsigned i;

    for (i = 0u; i < len; i++) {
        h ^= (uint32_t)buf[i];
        h *= 16777619u;
    }
    return h;
}

static int dual_read_exact(int fd, uint8_t *dst, unsigned len)
{
    unsigned done = 0u;

    while (done < len) {
        long r = a7r3_read(fd, dst + done, (size_t)(len - done));
        if (r <= 0)
            return -1;
        done += (unsigned)r;
    }
    return 0;
}

static int dual_load_runtime_payload(void)
{
    uint32_t hdr[3];
    uint8_t extra = 0u;
    int fd;

    g_dual_level_rec.tag = 0u;
    fd = a7r3_open(DUAL_HUD_BIN_PATH, A7R3_O_RDONLY, 0);
    if (fd < 0)
        return 0;

    if (dual_read_exact(fd, (uint8_t *)hdr, sizeof(hdr)) != 0 ||
        hdr[0] != DUAL_HUD_BIN_MAGIC ||
        hdr[1] != DUAL_HUD_PAYLOAD_LEN ||
        dual_read_exact(fd, g_dual_level_rec.payload, DUAL_HUD_PAYLOAD_LEN) != 0 ||
        a7r3_read(fd, &extra, 1u) != 0) {
        a7r3_close(fd);
        return 0;
    }
    a7r3_close(fd);

    if (dual_fnv1a32(g_dual_level_rec.payload, DUAL_HUD_PAYLOAD_LEN) != hdr[2])
        return 0;

    g_dual_level_rec.tag = 0x00000000u;
    a7r3_dsb();
    return 1;
}

__attribute__((noinline))
static unsigned dual_get_res_id_hook(void *rec_ptr)
{
    unsigned long lr = (unsigned long)(uintptr_t)__builtin_return_address(0);
    unsigned res_id = g_real_get_res_id(rec_ptr);

    if (g_dual_has_payload &&
        g_dual_enabled &&
        res_id == RES_ID_LEVEL_OUTER &&
        (lr & ~1UL) == (g_obj_base + LIBOBJ_PARSER_CALL_RET) &&
        rec_ptr != NULL) {
        void *orig_w0 = *(void *const *)rec_ptr;
        if (orig_w0 != (const void *)&g_dual_level_rec) {
            unsigned i;
            for (i = 0u; i < SAVED_REC_SLOTS; i++) {
                if (g_saved_recs[i].addr == rec_ptr || g_saved_recs[i].addr == NULL) {
                    g_saved_recs[i].orig_w0 = orig_w0;
                    a7r3_dsb();
                    g_saved_recs[i].addr = rec_ptr;
                    a7r3_dsb();
                    break;
                }
            }
            *(const void **)rec_ptr = (const void *)&g_dual_level_rec;
            a7r3_dsb();
        }
        g_dual_ever_parsed = 1;
        g_parse_hits++;
        a7r3_dsb();
    }
    return res_id;
}

__attribute__((noinline))
static void *dual_rec_dtor_hook(void *rec_ptr)
{
    if (rec_ptr != NULL &&
        *(const void *const *)rec_ptr == (const void *)&g_dual_level_rec) {
        unsigned i;
        for (i = 0u; i < SAVED_REC_SLOTS; i++) {
            if (g_saved_recs[i].addr == rec_ptr) {
                *(void **)rec_ptr = g_saved_recs[i].orig_w0;
                a7r3_dsb();
                g_saved_recs[i].addr = NULL;
                a7r3_dsb();
                break;
            }
        }
    }
    return g_real_rec_dtor(rec_ptr);
}

__attribute__((noinline))
static int dual_level_route_hook(void *self, unsigned widget_id, unsigned *out_master_id)
{
    if (g_dual_has_payload && (g_dual_enabled || g_dual_ever_parsed) && out_master_id != NULL) {
        if (widget_id == WID_HISTOGRAM) {
            *out_master_id = WID_NEW_MASTER;
            g_route_hits++;
            return 0;
        }
        if (widget_id == WID_NEW_MASTER) {
            *out_master_id = WID_ROOT_MASTER;
            g_route_hits++;
            return 0;
        }
    }
    return g_real_level_route(self, widget_id, out_master_id);
}

__attribute__((noinline))
static int dual_get_layout_mode_hook(void *self, int dev)
{
    unsigned long lr = (unsigned long)(uintptr_t)__builtin_return_address(0);
    int mode = g_real_get_layout_mode(self, dev);

    if (dev == 2)
        a7r3_pb_focus_on_disp_mode(mode);

    if (g_dual_has_payload &&
        g_dual_enabled &&
        mode == 4 &&
        (lr & ~1UL) == (g_vu2_base + VU2_GATE1_CALL_RET)) {
        g_gate1_hits++;
        return 5;
    }
    return mode;
}

int a7r3_dual_hud_install(void)
{
    unsigned long vu2_base;
    volatile unsigned long *layout_slot;

    if (g_dual_installed)
        return 0;

    vu2_base = a7r3_mod_base("/viewUnified2.so");
    if (!vu2_base) {
        a7r3_log_str("SDK_DUAL_NO_MOD\n");
        return -1;
    }

    layout_slot = (volatile unsigned long *)(vu2_base + VU2_GET_LAYOUT_MODE_GOT);
    if (!a7r3_pm_writable((const void *)layout_slot, 4u)) {
        a7r3_log_str("SDK_DUAL_BAD_SLOT\n");
        return -2;
    }

    g_vu2_base = vu2_base;
    g_orig_get_layout_got  = *layout_slot;
    g_real_get_layout_mode = (get_layout_mode_fn)(vu2_base + VU2_GET_LAYOUT_REAL);

    g_dual_has_payload = dual_load_runtime_payload();

    if (g_dual_has_payload) {
        unsigned long obj_base = a7r3_mod_base("/libObj.so");
        volatile unsigned long *resid_slot, *dtor_slot, *route_slot;
        unsigned i;

        if (!obj_base) {
            a7r3_log_str("SDK_DUAL_NO_MOD\n");
            return -1;
        }

        resid_slot = (volatile unsigned long *)(obj_base + LIBOBJ_GET_RESID_GOT);
        dtor_slot  = (volatile unsigned long *)(obj_base + LIBOBJ_REC_DTOR_GOT);
        route_slot = (volatile unsigned long *)(vu2_base + VU2_LEVEL_ROUTE_SLOT);

        if (!a7r3_pm_writable((const void *)resid_slot, 4u) ||
            !a7r3_pm_writable((const void *)dtor_slot, 4u) ||
            !a7r3_pm_writable((const void *)route_slot, 4u)) {
            a7r3_log_str("SDK_DUAL_BAD_SLOT\n");
            return -2;
        }

        if (*route_slot != (vu2_base + VU2_LEVEL_ROUTE_REAL)) {
            a7r3_log_hex("SDK_DUAL_ROUTE_MISMATCH=", *route_slot);
            return -3;
        }

        g_obj_base = obj_base;
        g_orig_get_resid_got = *resid_slot;
        g_orig_rec_dtor_got  = *dtor_slot;
        g_orig_route_slot    = *route_slot;

        g_real_get_res_id  = (get_res_id_fn)(obj_base + LIBOBJ_GET_RESID_REAL);
        g_real_rec_dtor    = (rec_dtor_fn)(obj_base + LIBOBJ_REC_DTOR_REAL);
        g_real_level_route = (level_route_fn)(vu2_base + VU2_LEVEL_ROUTE_REAL);

        for (i = 0u; i < SAVED_REC_SLOTS; i++) {
            g_saved_recs[i].addr = NULL;
            g_saved_recs[i].orig_w0 = NULL;
        }

        a7r3_dsb();
        /* Install ~RecordPtr hook before getResID hook so no redirected RecordPtr
         * can ever destruct without restoring orig_w0 first. */
        *dtor_slot = (unsigned long)(uintptr_t)&dual_rec_dtor_hook;
        a7r3_dsb();
        *resid_slot  = (unsigned long)(uintptr_t)&dual_get_res_id_hook;
        *layout_slot = (unsigned long)(uintptr_t)&dual_get_layout_mode_hook;
        *route_slot  = (unsigned long)(uintptr_t)&dual_level_route_hook;
        a7r3_dsb();

        g_get_resid_got  = resid_slot;
        g_rec_dtor_got   = dtor_slot;
        g_get_layout_got = layout_slot;
        g_route_slot     = route_slot;
        a7r3_log_hex("SDK_DUAL_PAYLOAD_OK=", 1UL);
    } else {
        a7r3_dsb();
        *layout_slot = (unsigned long)(uintptr_t)&dual_get_layout_mode_hook;
        a7r3_dsb();

        g_get_layout_got = layout_slot;
        a7r3_log_str("SDK_DUAL_NO_PAYLOAD_BIN\n");
    }

    g_dual_installed = 1;
    a7r3_dsb();

    a7r3_log_hex("SDK_DUAL_HOOKS=", 1UL);
    return 0;
}

void a7r3_dual_hud_remove(void)
{
    if (!g_dual_installed)
        return;

    g_dual_enabled = 0;
    a7r3_dsb();

    /* Restore getResID first and drain any active ResourceLayoutParser frame
     * before restoring ~RecordPtr. */
    if (g_get_resid_got) {
        *g_get_resid_got = g_orig_get_resid_got;
        g_get_resid_got = NULL;
        a7r3_dsb();
        a7r3_nanosleep_ms(30u);
    }

    if (g_rec_dtor_got) {
        *g_rec_dtor_got = g_orig_rec_dtor_got;
        g_rec_dtor_got = NULL;
    }
    if (g_route_slot) {
        *g_route_slot = g_orig_route_slot;
        g_route_slot = NULL;
    }
    if (g_get_layout_got) {
        *g_get_layout_got = g_orig_get_layout_got;
        g_get_layout_got = NULL;
    }
    a7r3_dsb();
    a7r3_nanosleep_ms(30u);

    g_dual_has_payload = 0;
    g_dual_installed = 0;
    a7r3_log_hex("SDK_DUAL_RESTORED=", 1UL);
}

int a7r3_dual_hud_has_payload(void)
{
    return g_dual_has_payload;
}

void a7r3_dual_hud_set_enabled(int enable)
{
    int on = enable ? 1 : 0;
    if (on && !g_dual_has_payload) {
        a7r3_log_str("SDK_DUAL_NO_PAYLOAD\n");
        g_dual_enabled = 0;
        return;
    }
    if (on == g_dual_enabled)
        return;
    g_dual_enabled = on;
    a7r3_dsb();
    a7r3_log_hex("SDK_DUAL_HUD=", (unsigned long)on);
}

int a7r3_dual_hud_get_enabled(void)
{
    return g_dual_enabled;
}

int a7r3_dual_hud_take_stats(unsigned *out_parse, unsigned *out_gate1, unsigned *out_route)
{
    unsigned p = g_parse_hits;
    unsigned g = g_gate1_hits;
    unsigned r = g_route_hits;

    if (p == g_last_parse_hits && g == g_last_gate1_hits && r == g_last_route_hits)
        return 0;

    g_last_parse_hits = p;
    g_last_gate1_hits = g;
    g_last_route_hits = r;

    if (out_parse)
        *out_parse = p;
    if (out_gate1)
        *out_gate1 = g;
    if (out_route)
        *out_route = r;
    return 1;
}
