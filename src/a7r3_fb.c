#include "a7r3_fb.h"
#include "a7r3_draw.h"
#include "a7r3_pb_focus.h"
#include "a7r3_sys.h"

#define BAND_COUNT   60u
#define BAND_BYTES   10240u
#define BAND_WORDS   2560u
#define BAND_SAMPLES 32u
#define BAND_STEP    79u

typedef int (*ioctl_fn)(int fd, unsigned long req, unsigned long arg);

static int g_mem_fd = -1;
static void *g_ren_map;
static void *g_flip_map;
static volatile uint16_t *g_ren_va;
static volatile uint16_t *g_flip_va;
static volatile uint16_t *g_flip_vb;
static int g_fb_ok;

static uint16_t g_pack_morton[A7R3_FB_PIXELS];
static uint16_t g_pack_linear[A7R3_FB_PIXELS];
static uint16_t g_saved_linear[A7R3_FB_PIXELS];
static uint16_t g_saved_morton[A7R3_FB_PIXELS];
static volatile int g_pack_ready;
static volatile int g_saved_ready;
static volatile int g_guard_active;

static volatile unsigned long *g_ioctl_got;
static ioctl_fn g_orig_ioctl;
static volatile unsigned g_ioh_calls;
static volatile unsigned g_ioh_repairs;

#define PA_RING_SZ 16u
static volatile unsigned long g_pa_ring[PA_RING_SZ];
static volatile unsigned g_pa_head;
static volatile unsigned g_pa_tail;
static volatile unsigned g_pa_budget;

#define PB_PX_MAX 640u
struct pb_px {
    uint16_t x;
    uint16_t y;
    uint16_t orig;
    uint16_t drawn;
};

static struct pb_px g_pb_px[PB_PX_MAX];
static volatile unsigned g_pb_px_cnt;
static volatile int g_pb_box_active;
static volatile int g_pb_in_ioctl;
static volatile int g_pb_updating;
static int g_pb_cur_cx = -1;
static int g_pb_cur_cy = -1;
static int g_pb_cur_hw = -1;
static int g_pb_cur_hh = -1;

static void blit32(volatile void *dst_p, const void *src_p, unsigned bytes)
{
    volatile uint32_t *d = (volatile uint32_t *)dst_p;
    const uint32_t *s = (const uint32_t *)src_p;
    unsigned n = bytes >> 4;

    while (n--) {
        uint32_t w0 = s[0], w1 = s[1], w2 = s[2], w3 = s[3];
        d[0] = w0;
        d[1] = w1;
        d[2] = w2;
        d[3] = w3;
        s += 4;
        d += 4;
    }
    a7r3_dsb();
}

static unsigned repair_surface_bands(volatile void *dst_p, const void *src_p)
{
    volatile uint32_t *d = (volatile uint32_t *)dst_p;
    const uint32_t *s = (const uint32_t *)src_p;
    unsigned b, k, fixed = 0;

    if (!g_guard_active || !g_pack_ready || !dst_p)
        return 0;
    for (b = 0; b < BAND_COUNT; b++) {
        unsigned base = b * BAND_WORDS;
        unsigned col_off = (b * 13u) & 63u;
        int dirty = 0;
        for (k = 0; k < BAND_SAMPLES; k++) {
            unsigned idx = base + k * BAND_STEP + col_off;
            if (d[idx] != s[idx]) {
                dirty = 1;
                break;
            }
        }
        if (dirty) {
            if (!g_guard_active)
                return fixed;
            blit32((volatile void *)(d + base), (const void *)(s + base), BAND_BYTES);
            fixed++;
        }
    }
    return fixed;
}

static unsigned repair_band_exact(volatile void *dst_p, const void *src_p, unsigned b)
{
    volatile uint32_t *d = (volatile uint32_t *)dst_p + b * BAND_WORDS;
    const uint32_t *s = (const uint32_t *)src_p + b * BAND_WORDS;
    unsigned i;

    if (!g_guard_active || !g_pack_ready || !dst_p)
        return 0;
    for (i = 0; i < BAND_WORDS; i++) {
        if (d[i] != s[i]) {
            if (!g_guard_active)
                return 0;
            blit32((volatile void *)d, (const void *)s, BAND_BYTES);
            return 1;
        }
    }
    return 0;
}

int a7r3_fb_open(void)
{
    if (g_fb_ok)
        return 0;
    if (g_mem_fd < 0) {
        g_mem_fd = a7r3_open("/dev/mem", A7R3_O_RDWR_SYNC, 0);
        if (g_mem_fd < 0)
            return -1;
    }
    if (!g_ren_map) {
        g_ren_map = a7r3_mmap2(NULL, 0x98000, A7R3_PROT_READ | A7R3_PROT_WRITE,
                               A7R3_MAP_SHARED, g_mem_fd, A7R3_PA_RENDER >> 12);
        if (!g_ren_map)
            return -2;
        g_ren_va = (volatile uint16_t *)((uintptr_t)g_ren_map + (A7R3_PA_RENDER & 0xfffUL));
    }
    if (!g_flip_map) {
        g_flip_map = a7r3_mmap2(NULL, 0x200000, A7R3_PROT_READ | A7R3_PROT_WRITE,
                                A7R3_MAP_SHARED, g_mem_fd, 0x23A00000UL >> 12);
        if (!g_flip_map)
            return -3;
        g_flip_va = (volatile uint16_t *)((uintptr_t)g_flip_map + (A7R3_PA_FLIP_A - 0x23A00000UL));
        g_flip_vb = (volatile uint16_t *)((uintptr_t)g_flip_map + (A7R3_PA_FLIP_B - 0x23A00000UL));
    }
    g_fb_ok = 1;
    return 0;
}

void a7r3_fb_close(void)
{
    g_guard_active = 0;
    g_pack_ready = 0;
    a7r3_dsb();
    if (g_ren_map) {
        a7r3_munmap(g_ren_map, 0x98000);
        g_ren_map = NULL;
        g_ren_va = NULL;
    }
    if (g_flip_map) {
        a7r3_munmap(g_flip_map, 0x200000);
        g_flip_map = NULL;
        g_flip_va = NULL;
        g_flip_vb = NULL;
    }
    if (g_mem_fd >= 0) {
        a7r3_close(g_mem_fd);
        g_mem_fd = -1;
    }
    g_fb_ok = 0;
}

void a7r3_fb_pack_frame(const uint16_t *lin_rgba4444)
{
    unsigned x, y;
    if (!lin_rgba4444)
        return;
    g_pack_ready = 0;
    a7r3_dsb();
    for (y = 0; y < A7R3_FB_H; y++) {
        unsigned yy = (A7R3_FB_H - 1u) - y;
        unsigned rowbase = (yy >> 3) * (A7R3_FB_W >> 3);
        const uint16_t *srow = &lin_rgba4444[y * A7R3_FB_W];
        uint16_t *drow = &g_pack_linear[y * A7R3_FB_W];
        for (x = 0; x < A7R3_FB_W; x++) {
            uint16_t px = srow[x];
            unsigned z = (x & 1u) | ((yy & 1u) << 1) | ((x & 2u) << 1) |
                         ((yy & 2u) << 2) | ((x & 4u) << 2) | ((yy & 4u) << 3);
            drow[x] = px;
            g_pack_morton[(rowbase + (x >> 3)) * 64u + z] = px;
        }
    }
    a7r3_dsb();
    g_pack_ready = 1;
}

void a7r3_fb_present_full(void)
{
    if (!g_pack_ready || a7r3_fb_open() != 0)
        return;
    blit32(g_flip_va, g_pack_linear, A7R3_FB_BYTES);
    blit32(g_flip_vb, g_pack_linear, A7R3_FB_BYTES);
    blit32(g_ren_va, g_pack_morton, A7R3_FB_BYTES);
}

void a7r3_fb_blit_rect(int x0, int y0, int w, int h,
                       const uint16_t *src_rgba4444, int src_stride)
{
    int x, y;
    int x1, y1;

    if (!src_rgba4444 || w <= 0 || h <= 0 || a7r3_fb_open() != 0)
        return;
    x1 = x0 + w;
    y1 = y0 + h;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int)A7R3_FB_W)
        x1 = (int)A7R3_FB_W;
    if (y1 > (int)A7R3_FB_H)
        y1 = (int)A7R3_FB_H;

    for (y = y0; y < y1; y++) {
        const uint16_t *srow = src_rgba4444 + y * src_stride;
        unsigned dst_off = (unsigned)y * A7R3_FB_W;
        for (x = x0; x < x1; x++) {
            uint16_t px = srow[x];
            g_flip_va[dst_off + (unsigned)x] = px;
            g_flip_vb[dst_off + (unsigned)x] = px;
            g_ren_va[a7r3_morton_index((unsigned)x, (unsigned)y)] = px;
        }
    }
    a7r3_dsb();
}

int a7r3_fb_grab_native(uint16_t *dst_lin)
{
    unsigned i, sum = 0;
    if (!dst_lin || a7r3_fb_open() != 0)
        return -1;
    a7r3_dsb();
    for (i = 0; i < A7R3_FB_PIXELS; i++) {
        uint16_t px = g_flip_va[i];
        dst_lin[i] = px;
        sum += px;
    }
    return sum ? 0 : -2;
}

void a7r3_fb_restore_native(const uint16_t *src_lin)
{
    if (!src_lin)
        return;
    a7r3_fb_pack_frame(src_lin);
    a7r3_fb_present_full();
}

int a7r3_fb_save_native(uint16_t *dst_lin_opt)
{
    unsigned i, sum = 0, x, y;
    if (a7r3_fb_open() != 0)
        return -1;
    g_saved_ready = 0;
    a7r3_dsb();
    for (i = 0; i < A7R3_FB_PIXELS; i++) {
        uint16_t px = g_flip_va[i];
        g_saved_linear[i] = px;
        if (dst_lin_opt)
            dst_lin_opt[i] = px;
        sum += px;
    }
    if (!sum)
        return -2;
    for (y = 0; y < A7R3_FB_H; y++) {
        unsigned yy = (A7R3_FB_H - 1u) - y;
        unsigned rowbase = (yy >> 3) * (A7R3_FB_W >> 3);
        const uint16_t *srow = &g_saved_linear[y * A7R3_FB_W];
        for (x = 0; x < A7R3_FB_W; x++) {
            unsigned z = (x & 1u) | ((yy & 1u) << 1) | ((x & 2u) << 1) |
                         ((yy & 2u) << 2) | ((x & 4u) << 2) | ((yy & 4u) << 3);
            g_saved_morton[(rowbase + (x >> 3)) * 64u + z] = srow[x];
        }
    }
    a7r3_dsb();
    g_saved_ready = 1;
    a7r3_dsb();
    return 0;
}

void a7r3_fb_restore_saved(void)
{
    if (!g_saved_ready || !g_fb_ok)
        return;
    blit32(g_flip_va, g_saved_linear, A7R3_FB_BYTES);
    blit32(g_flip_vb, g_saved_linear, A7R3_FB_BYTES);
    blit32(g_ren_va, g_saved_morton, A7R3_FB_BYTES);
    g_saved_ready = 0;
    a7r3_dsb();
}

unsigned a7r3_fb_repair_tick(void)
{
    static unsigned exact_b;
    unsigned fixed = 0, step;

    if (!g_guard_active || !g_fb_ok || !g_pack_ready)
        return 0;
    fixed += repair_surface_bands(g_ren_va, g_pack_morton);
    fixed += repair_surface_bands(g_flip_va, g_pack_linear);
    fixed += repair_surface_bands(g_flip_vb, g_pack_linear);
    for (step = 0; step < 2u; step++) {
        unsigned b = exact_b;
        exact_b = (exact_b + 1u >= BAND_COUNT) ? 0u : exact_b + 1u;
        fixed += repair_band_exact(g_ren_va, g_pack_morton, b);
        fixed += repair_band_exact(g_flip_va, g_pack_linear, b);
        fixed += repair_band_exact(g_flip_vb, g_pack_linear, b);
    }
    return fixed;
}

static void record_pa(unsigned long pa)
{
    unsigned h, t, i, n;
    if (pa < 0x20000000UL || pa >= 0x28000000UL || (pa & 0xffUL) != 0UL)
        return;
    h = g_pa_head;
    t = g_pa_tail;
    n = h - t;
    if (n > PA_RING_SZ)
        n = PA_RING_SZ;
    for (i = 0; i < n; i++) {
        if (g_pa_ring[(t + i) & (PA_RING_SZ - 1u)] == pa)
            return;
    }
    if ((h - t) < PA_RING_SZ) {
        g_pa_ring[h & (PA_RING_SZ - 1u)] = pa;
        a7r3_dsb();
        g_pa_head = h + 1u;
    }
}

static void sniff_gpu_ioctl(unsigned long req, unsigned long arg)
{
    unsigned long w1;
    const unsigned long *buf;
    unsigned i;

    if (!g_pa_budget)
        return;
    if (req != 0xc0048211UL && req != 0xc0048212UL)
        return;
    if (arg <= 0x10000UL || !a7r3_pm_readable((const void *)arg, 16u))
        return;
    g_pa_budget--;
    w1 = ((const unsigned long *)arg)[1];
    if (w1 <= 0x10000UL || !a7r3_pm_readable((const void *)w1, 256u))
        return;
    buf = (const unsigned long *)w1;
    for (i = 0; i < 64u; i++)
        record_pa(buf[i]);
}

static void pb_set_px(int x, int y, uint16_t color)
{
    unsigned i, n = g_pb_px_cnt;
    if (x < 0 || x >= (int)A7R3_FB_W || y < 0 || y >= (int)A7R3_FB_H)
        return;
    for (i = 0u; i < n; i++) {
        if ((int)g_pb_px[i].x == x && (int)g_pb_px[i].y == y) {
            g_pb_px[i].drawn = color;
            return;
        }
    }
    if (n < PB_PX_MAX) {
        g_pb_px[n].x = (uint16_t)x;
        g_pb_px[n].y = (uint16_t)y;
        g_pb_px[n].orig = 0u;
        g_pb_px[n].drawn = color;
        g_pb_px_cnt = n + 1u;
    }
}

static void pb_fill_rect_list(int x0, int y0, int w, int h, uint16_t color)
{
    int dx, dy;
    for (dy = 0; dy < h; dy++)
        for (dx = 0; dx < w; dx++)
            pb_set_px(x0 + dx, y0 + dy, color);
}

static void pb_build_reticle(int cx, int cy, int hw, int hh)
{
    uint16_t col_fg = A7R3_RGBA4444(2, 15, 4, 15);
    uint16_t col_bg = A7R3_RGBA4444(0, 0, 0, 14);
    int x0 = cx - hw, y0 = cy - hh;
    int x1 = cx + hw, y1 = cy + hh;
    int arm = 9;
    int t = 2;

    if (arm > hw)
        arm = hw;
    if (arm > hh)
        arm = hh;
    g_pb_px_cnt = 0u;

    /* 1px dark outline around 4 corner brackets & center dot */
    pb_fill_rect_list(x0 - 1, y0 - 1, arm + 2, t + 2, col_bg);
    pb_fill_rect_list(x0 - 1, y0 - 1, t + 2, arm + 2, col_bg);

    pb_fill_rect_list(x1 - arm, y0 - 1, arm + 2, t + 2, col_bg);
    pb_fill_rect_list(x1 - 1, y0 - 1, t + 2, arm + 2, col_bg);

    pb_fill_rect_list(x0 - 1, y1 - 1, arm + 2, t + 2, col_bg);
    pb_fill_rect_list(x0 - 1, y1 - arm, t + 2, arm + 2, col_bg);

    pb_fill_rect_list(x1 - arm, y1 - 1, arm + 2, t + 2, col_bg);
    pb_fill_rect_list(x1 - 1, y1 - arm, t + 2, arm + 2, col_bg);

    pb_fill_rect_list(cx - 2, cy - 2, 4, 4, col_bg);

    /* Bright green 2px corner brackets & 2x2 center dot */
    pb_fill_rect_list(x0, y0, arm, t, col_fg);
    pb_fill_rect_list(x0, y0, t, arm, col_fg);

    pb_fill_rect_list(x1 - arm + 1, y0, arm, t, col_fg);
    pb_fill_rect_list(x1 - 1, y0, t, arm, col_fg);

    pb_fill_rect_list(x0, y1 - 1, arm, t, col_fg);
    pb_fill_rect_list(x0, y1 - arm + 1, t, arm, col_fg);

    pb_fill_rect_list(x1 - arm + 1, y1 - 1, arm, t, col_fg);
    pb_fill_rect_list(x1 - 1, y1 - arm + 1, t, arm, col_fg);

    pb_fill_rect_list(cx - 1, cy - 1, 2, 2, col_fg);
}

static void pb_stamp_pixels(int refresh_orig)
{
    unsigned i, n = g_pb_px_cnt;
    if (!g_fb_ok || !n)
        return;
    for (i = 0u; i < n; i++) {
        unsigned x = g_pb_px[i].x;
        unsigned y = g_pb_px[i].y;
        unsigned off = y * A7R3_FB_W + x;
        uint16_t cur = g_flip_va[off];
        uint16_t drawn = g_pb_px[i].drawn;
        if (refresh_orig && cur != drawn)
            g_pb_px[i].orig = cur;
        g_flip_va[off] = drawn;
        g_flip_vb[off] = drawn;
        g_ren_va[a7r3_morton_index(x, y)] = drawn;
    }
    a7r3_dsb();
}

void a7r3_fb_pb_box_hide(void)
{
    unsigned i, n;
    int spin;

    if (!g_pb_box_active && g_pb_px_cnt == 0u)
        return;
    g_pb_updating = 1;
    g_pb_box_active = 0;
    a7r3_dsb();
    for (spin = 0; spin < 20 && g_pb_in_ioctl; spin++)
        a7r3_nanosleep_ms(1u);

    n = g_pb_px_cnt;
    if (g_fb_ok && n) {
        for (i = 0u; i < n; i++) {
            unsigned x = g_pb_px[i].x;
            unsigned y = g_pb_px[i].y;
            unsigned off = y * A7R3_FB_W + x;
            uint16_t drawn = g_pb_px[i].drawn;
            uint16_t orig = g_pb_px[i].orig;
            if (g_flip_va[off] == drawn)
                g_flip_va[off] = orig;
            if (g_flip_vb[off] == drawn)
                g_flip_vb[off] = orig;
            if (g_ren_va[a7r3_morton_index(x, y)] == drawn)
                g_ren_va[a7r3_morton_index(x, y)] = orig;
        }
        a7r3_dsb();
    }
    g_pb_px_cnt = 0u;
    g_pb_cur_cx = -1;
    g_pb_cur_cy = -1;
    g_pb_cur_hw = -1;
    g_pb_cur_hh = -1;
    a7r3_dsb();
    g_pb_updating = 0;
    a7r3_dsb();
}

void a7r3_fb_pb_box_show(int cx, int cy, int half_w, int half_h)
{
    int spin;

    if (cx < 0)
        cx = 0;
    else if (cx >= (int)A7R3_FB_W)
        cx = (int)A7R3_FB_W - 1;
    if (cy < 0)
        cy = 0;
    else if (cy >= (int)A7R3_FB_H)
        cy = (int)A7R3_FB_H - 1;
    if (half_w < 4)
        half_w = 4;
    if (half_h < 4)
        half_h = 4;
    if (g_guard_active || a7r3_fb_open() != 0)
        return;

    if (g_pb_box_active &&
        g_pb_cur_cx == cx && g_pb_cur_cy == cy &&
        g_pb_cur_hw == half_w && g_pb_cur_hh == half_h) {
        unsigned n = g_pb_px_cnt;
        if (n == 0u)
            return;
        {
            unsigned off0 = (unsigned)g_pb_px[0].y * A7R3_FB_W + (unsigned)g_pb_px[0].x;
            unsigned off1 = (unsigned)g_pb_px[n - 1u].y * A7R3_FB_W + (unsigned)g_pb_px[n - 1u].x;
            if (g_flip_va[off0] == g_pb_px[0].drawn &&
                g_flip_va[off1] == g_pb_px[n - 1u].drawn)
                return;
        }
        g_pb_updating = 1;
        a7r3_dsb();
        for (spin = 0; spin < 20 && g_pb_in_ioctl; spin++)
            a7r3_nanosleep_ms(1u);
        pb_stamp_pixels(1);
        g_pb_updating = 0;
        a7r3_dsb();
        return;
    }

    a7r3_fb_pb_box_hide();
    g_pb_updating = 1;
    a7r3_dsb();
    for (spin = 0; spin < 20 && g_pb_in_ioctl; spin++)
        a7r3_nanosleep_ms(1u);
    pb_build_reticle(cx, cy, half_w, half_h);
    pb_stamp_pixels(1);
    g_pb_cur_cx = cx;
    g_pb_cur_cy = cy;
    g_pb_cur_hw = half_w;
    g_pb_cur_hh = half_h;
    a7r3_dsb();
    g_pb_box_active = 1;
    a7r3_dsb();
    g_pb_updating = 0;
    a7r3_dsb();
}

static int ioctl_hook_fn(int fd, unsigned long req, unsigned long arg)
{
    int rc;
    int guard = (g_guard_active && g_pack_ready);
    int pb_box = (!g_guard_active && g_pb_box_active);

    if (!g_fb_ok || (req & 0xff00u) != 0x8200u || (!guard && !pb_box))
        return g_orig_ioctl(fd, req, arg);

    g_ioh_calls++;
    sniff_gpu_ioctl(req, arg);
    if (guard && req == 0xc0048211UL)
        g_ioh_repairs += repair_surface_bands(g_ren_va, g_pack_morton);
    rc = g_orig_ioctl(fd, req, arg);
    if (req == 0xc0048211UL || req == 0xc0048212UL) {
        if (guard && g_guard_active) {
            g_ioh_repairs += repair_surface_bands(g_flip_va, g_pack_linear);
            g_ioh_repairs += repair_surface_bands(g_flip_vb, g_pack_linear);
        } else if (!g_guard_active && g_pb_box_active && !g_pb_updating &&
                   a7r3_pb_focus_allow_stamp()) {
            g_pb_in_ioctl = 1;
            a7r3_dsb();
            if (!g_guard_active && g_pb_box_active && !g_pb_updating &&
                a7r3_pb_focus_allow_stamp())
                pb_stamp_pixels(0);
            g_pb_in_ioctl = 0;
            a7r3_dsb();
        }
    }
    return rc;
}

int a7r3_fb_install_ioctl_hook(void)
{
    unsigned long obj_base, libc_lo, orig;
    volatile unsigned long *slot;

    if (g_ioctl_got)
        return 0;
    obj_base = a7r3_mod_base("/libObj.so");
    libc_lo = a7r3_mod_base("/libc-2.11.2.so");
    if (!obj_base || !libc_lo)
        return -1;
    slot = (volatile unsigned long *)(obj_base + A7R3_LIBOBJ_IOCTL_GOT);
    if (!a7r3_pm_writable((const void *)slot, 4))
        return -2;
    orig = *slot;
    if (orig < libc_lo || orig > libc_lo + 0x200000UL)
        return -3;
    g_orig_ioctl = (ioctl_fn)orig;
    a7r3_dsb();
    *slot = (unsigned long)(uintptr_t)&ioctl_hook_fn;
    a7r3_dsb();
    g_ioctl_got = slot;
    a7r3_log_hex("SDK_IOCTL_HOOK=", (unsigned long)slot);
    a7r3_log_hex("SDK_IOCTL_ORIG=", orig);
    return 0;
}

void a7r3_fb_remove_ioctl_hook(void)
{
    g_guard_active = 0;
    a7r3_dsb();
    if (!g_ioctl_got || !g_orig_ioctl)
        return;
    *g_ioctl_got = (unsigned long)(uintptr_t)g_orig_ioctl;
    a7r3_dsb();
    a7r3_nanosleep_ms(50);
    a7r3_log_hex("SDK_IOCTL_RESTORED=", (unsigned long)g_ioctl_got);
    g_ioctl_got = NULL;
}

void a7r3_fb_set_guard_active(int active)
{
    g_guard_active = active ? 1 : 0;
    a7r3_dsb();
}

void a7r3_fb_arm_pa_probe(unsigned budget)
{
    g_pa_budget = budget;
    a7r3_dsb();
}

int a7r3_fb_pop_probed_pa(unsigned long *out_pa)
{
    unsigned t = g_pa_tail;
    if (t == g_pa_head)
        return 0;
    if (out_pa)
        *out_pa = g_pa_ring[t & (PA_RING_SZ - 1u)];
    a7r3_dsb();
    g_pa_tail = t + 1u;
    return 1;
}
