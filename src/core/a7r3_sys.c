#include "a7r3_sys.h"

#define A7R3_PM_RNG_MAX 2048

struct a7r3_pm_rng {
    unsigned long lo, hi;
    int writable;
};

static struct a7r3_pm_rng g_rng[2][A7R3_PM_RNG_MAX];
static int g_rng_n[2];
static volatile int g_rng_idx;
static volatile int g_rng_loaded;
static volatile unsigned g_maps_lock;
static unsigned g_last_reload_ms;
static char g_maps_buf[131072];
static char g_log_path[128] = "/tmp_bt/addon.log";

long a7r3_sys2(long nr, long a1, long a2)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a1;
    register long r1 __asm__("r1") = a2;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

long a7r3_sys3(long nr, long a1, long a2, long a3)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a1;
    register long r1 __asm__("r1") = a2;
    register long r2 __asm__("r2") = a3;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

long a7r3_sys4(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a1;
    register long r1 __asm__("r1") = a2;
    register long r2 __asm__("r2") = a3;
    register long r3 __asm__("r3") = a4;
    register long r4 __asm__("r4") = a5;
    register long r5 __asm__("r5") = a6;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
}

int a7r3_open(const char *path, int flags, int mode)
{
    return (int)a7r3_sys3(A7R3_SYS_OPEN, (long)path, flags, mode);
}

int a7r3_close(int fd)
{
    return (int)a7r3_sys2(A7R3_SYS_CLOSE, fd, 0);
}

long a7r3_read(int fd, void *buf, size_t n)
{
    return a7r3_sys3(A7R3_SYS_READ, fd, (long)buf, (long)n);
}

long a7r3_write(int fd, const void *buf, size_t n)
{
    return a7r3_sys3(A7R3_SYS_WRITE, fd, (long)buf, (long)n);
}

void a7r3_unlink(const char *path)
{
    a7r3_sys2(A7R3_SYS_UNLINK, (long)path, 0);
}

int a7r3_file_exists(const char *path)
{
    int fd = a7r3_open(path, A7R3_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    a7r3_close(fd);
    return 1;
}

void *a7r3_mmap2(void *addr, size_t len, int prot, int flags, int fd, unsigned long pgoff)
{
    long m = a7r3_sys4(A7R3_SYS_MMAP2, (long)addr, (long)len, prot, flags, fd, (long)pgoff);
    if (m < 0 && m > -4096)
        return NULL;
    return (void *)m;
}

int a7r3_munmap(void *addr, size_t len)
{
    return (int)a7r3_sys2(A7R3_SYS_MUNMAP, (long)addr, (long)len);
}

void a7r3_nanosleep_ms(unsigned ms)
{
    struct a7r3_timespec32 ts;
    ts.tv_sec = (int)(ms / 1000u);
    ts.tv_nsec = (int)((ms % 1000u) * 1000000u);
    a7r3_sys2(A7R3_SYS_NANOSLEEP, (long)&ts, 0);
}

unsigned a7r3_now_ms(void)
{
    struct a7r3_timespec32 ts;
    if (a7r3_sys2(A7R3_SYS_CLOCK_GETTIME, A7R3_CLOCK_MONOTONIC, (long)&ts) != 0)
        return 0;
    return (unsigned)ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000);
}

void a7r3_futex_wait(volatile unsigned *addr, unsigned expected, unsigned timeout_ms)
{
    if (timeout_ms == 0u) {
        a7r3_sys4(A7R3_SYS_FUTEX, (long)addr, A7R3_FUTEX_WAIT_PRIV, (long)expected, 0, 0, 0);
    } else {
        struct a7r3_timespec32 ts;
        ts.tv_sec = (int)(timeout_ms / 1000u);
        ts.tv_nsec = (int)((timeout_ms % 1000u) * 1000000u);
        a7r3_sys4(A7R3_SYS_FUTEX, (long)addr, A7R3_FUTEX_WAIT_PRIV, (long)expected, (long)&ts, 0, 0);
    }
}

void a7r3_futex_wake(volatile unsigned *addr)
{
    a7r3_sys4(A7R3_SYS_FUTEX, (long)addr, A7R3_FUTEX_WAKE_PRIV, 1, 0, 0, 0);
}

static int lower_ch(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int a7r3_bytes_eq(const char *a, const char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

int a7r3_str_has(const char *s, const char *needle)
{
    int i;
    while (s && *s) {
        for (i = 0; needle[i]; i++) {
            if (lower_ch(s[i]) != lower_ch(needle[i]))
                break;
        }
        if (!needle[i])
            return 1;
        s++;
    }
    return 0;
}

int a7r3_comm_is_im(void)
{
    char buf[32];
    int fd = a7r3_open("/proc/self/comm", A7R3_O_RDONLY, 0);
    long n;
    if (fd < 0)
        return 0;
    n = a7r3_read(fd, buf, sizeof(buf) - 1);
    a7r3_close(fd);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    return a7r3_bytes_eq(buf, "im.elf", 6);
}

static unsigned long parse_hex(const char **s)
{
    unsigned long v = 0;
    while (**s) {
        char c = **s;
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;
        v = (v << 4) | (unsigned long)d;
        (*s)++;
    }
    return v;
}

static void pm_lock_acquire(void)
{
    while (__atomic_exchange_n(&g_maps_lock, 1u, __ATOMIC_ACQUIRE) != 0u)
        a7r3_nanosleep_ms(1u);
}

static void pm_lock_release(void)
{
    __atomic_store_n(&g_maps_lock, 0u, __ATOMIC_RELEASE);
}

static int pm_read_maps_locked(void)
{
    int fd = a7r3_open("/proc/self/maps", A7R3_O_RDONLY, 0);
    int n = 0;

    if (fd < 0)
        return -1;
    while (n < (int)sizeof(g_maps_buf) - 1) {
        long r = a7r3_read(fd, g_maps_buf + n, sizeof(g_maps_buf) - 1u - (size_t)n);
        if (r <= 0)
            break;
        n += (int)r;
    }
    a7r3_close(fd);
    if (n <= 0)
        return -1;
    g_maps_buf[n] = 0;
    return n;
}

static void pm_reload_locked(void)
{
    int n = pm_read_maps_locked();
    int next = __atomic_load_n(&g_rng_idx, __ATOMIC_RELAXED) ^ 1;
    struct a7r3_pm_rng *dst = g_rng[next];
    int cnt = 0;
    const char *p, *end;

    if (n <= 0) {
        __atomic_store_n(&g_rng_loaded, 1, __ATOMIC_RELEASE);
        g_last_reload_ms = a7r3_now_ms();
        return;
    }

    p = g_maps_buf;
    end = g_maps_buf + n;
    while (p < end && cnt < A7R3_PM_RNG_MAX) {
        const char *line = p, *next_line, *q;
        unsigned long lo, hi;
        while (p < end && *p != '\n')
            p++;
        next_line = (p < end) ? p + 1 : end;
        q = line;
        lo = parse_hex(&q);
        if (q < p && *q == '-') {
            q++;
            hi = parse_hex(&q);
            if (q + 2 < p && q[0] == ' ' && q[1] == 'r' && hi > lo) {
                dst[cnt].lo = lo;
                dst[cnt].hi = hi;
                dst[cnt].writable = (q[2] == 'w');
                cnt++;
            }
        }
        p = next_line;
    }

    g_rng_n[next] = cnt;
    a7r3_dsb();
    __atomic_store_n(&g_rng_idx, next, __ATOMIC_RELEASE);
    __atomic_store_n(&g_rng_loaded, 1, __ATOMIC_RELEASE);
    g_last_reload_ms = a7r3_now_ms();
}

static int pm_find_range(const struct a7r3_pm_rng *rng, int n,
                         unsigned long a, unsigned long b, int need_write)
{
    int i;
    for (i = 0; i < n; i++) {
        if ((!need_write || rng[i].writable) && a >= rng[i].lo && b <= rng[i].hi)
            return 1;
    }
    return 0;
}

static int pm_check_range(const void *p, unsigned len, int need_write)
{
    unsigned long a = (unsigned long)p;
    unsigned long b = a + len;
    int idx, ok;
    unsigned now;

    if (!len || a < 0x8000UL || b <= a)
        return 0;

    if (__atomic_load_n(&g_rng_loaded, __ATOMIC_ACQUIRE)) {
        idx = __atomic_load_n(&g_rng_idx, __ATOMIC_ACQUIRE);
        if (pm_find_range(g_rng[idx], g_rng_n[idx], a, b, need_write))
            return 1;
    }

    /* Newly mapped heap/mmap arenas (e.g. menu widgets created after startup)
     * miss the initial cache; refresh maps lazily before rejecting access. */
    pm_lock_acquire();
    idx = __atomic_load_n(&g_rng_idx, __ATOMIC_RELAXED);
    if (__atomic_load_n(&g_rng_loaded, __ATOMIC_RELAXED) &&
        pm_find_range(g_rng[idx], g_rng_n[idx], a, b, need_write)) {
        pm_lock_release();
        return 1;
    }
    now = a7r3_now_ms();
    if (!__atomic_load_n(&g_rng_loaded, __ATOMIC_RELAXED) ||
        (unsigned)(now - g_last_reload_ms) >= 10u) {
        pm_reload_locked();
        idx = __atomic_load_n(&g_rng_idx, __ATOMIC_RELAXED);
    }
    ok = __atomic_load_n(&g_rng_loaded, __ATOMIC_RELAXED)
             ? pm_find_range(g_rng[idx], g_rng_n[idx], a, b, need_write)
             : 0;
    pm_lock_release();
    return ok;
}

void a7r3_maps_reload(void)
{
    pm_lock_acquire();
    pm_reload_locked();
    pm_lock_release();
}

unsigned long a7r3_mod_base(const char *needle)
{
    int n;
    char *p, *end;
    unsigned long best = 0;

    pm_lock_acquire();
    n = pm_read_maps_locked();
    if (n <= 0) {
        pm_lock_release();
        return 0;
    }
    p = g_maps_buf;
    end = g_maps_buf + n;
    while (p < end) {
        char *line = p;
        const char *q;
        unsigned long lo;
        while (p < end && *p != '\n')
            p++;
        if (p < end)
            *p++ = 0;
        if (a7r3_str_has(line, needle)) {
            q = line;
            lo = parse_hex(&q);
            if (lo && (!best || lo < best))
                best = lo;
        }
    }
    pm_lock_release();
    return best;
}

int a7r3_pm_readable(const void *p, unsigned len)
{
    return pm_check_range(p, len, 0);
}

int a7r3_pm_writable(const void *p, unsigned len)
{
    return pm_check_range(p, len, 1);
}

void a7r3_log_init(const char *path)
{
    unsigned i = 0;
    int fd;
    if (path) {
        while (path[i] && i + 1 < sizeof(g_log_path)) {
            g_log_path[i] = path[i];
            i++;
        }
        g_log_path[i] = 0;
    }
    fd = a7r3_open(g_log_path, A7R3_O_WRONLY | A7R3_O_CREAT | A7R3_O_TRUNC, 0644);
    if (fd >= 0)
        a7r3_close(fd);
}

void a7r3_log_str(const char *s)
{
    size_t len = 0;
    int fd;
    if (!s)
        return;
    while (s[len])
        len++;
    fd = a7r3_open(g_log_path, A7R3_O_WRONLY | A7R3_O_CREAT | A7R3_O_APPEND, 0644);
    if (fd >= 0) {
        a7r3_write(fd, s, len);
        a7r3_close(fd);
    }
}

void a7r3_log_hex(const char *prefix, unsigned long v)
{
    static const char hx[] = "0123456789abcdef";
    char buf[96];
    int n = 0, i, started = 0;

    if (prefix) {
        while (*prefix && n < 70)
            buf[n++] = *prefix++;
    }
    buf[n++] = '0';
    buf[n++] = 'x';
    for (i = 28; i >= 0; i -= 4) {
        unsigned d = (unsigned)((v >> i) & 0xfu);
        if (d || started || i == 0) {
            started = 1;
            buf[n++] = hx[d];
        }
    }
    buf[n++] = '\n';
    buf[n] = 0;
    a7r3_log_str(buf);
}
