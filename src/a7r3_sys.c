#include "a7r3_sys.h"

#define A7R3_PM_RNG_MAX 2048

struct a7r3_pm_rng {
    unsigned long lo, hi;
    int writable;
};

static struct a7r3_pm_rng g_rng[A7R3_PM_RNG_MAX];
static int g_rng_n;
static int g_rng_loaded;
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

static void pm_load_maps(void)
{
    static char buf[131072];
    int fd, n = 0;
    const char *p, *end;

    if (g_rng_loaded)
        return;
    g_rng_loaded = 1;
    fd = a7r3_open("/proc/self/maps", A7R3_O_RDONLY, 0);
    if (fd < 0)
        return;
    while (n < (int)sizeof(buf) - 1) {
        long r = a7r3_read(fd, buf + n, sizeof(buf) - 1u - (size_t)n);
        if (r <= 0)
            break;
        n += (int)r;
    }
    a7r3_close(fd);
    if (n <= 0)
        return;
    buf[n] = 0;
    p = buf;
    end = buf + n;
    while (p < end && g_rng_n < A7R3_PM_RNG_MAX) {
        const char *line = p, *next, *q;
        unsigned long lo, hi;
        while (p < end && *p != '\n')
            p++;
        next = (p < end) ? p + 1 : end;
        q = line;
        lo = parse_hex(&q);
        if (q < p && *q == '-') {
            q++;
            hi = parse_hex(&q);
            if (q + 2 < p && q[0] == ' ' && q[1] == 'r' && hi > lo) {
                g_rng[g_rng_n].lo = lo;
                g_rng[g_rng_n].hi = hi;
                g_rng[g_rng_n].writable = (q[2] == 'w');
                g_rng_n++;
            }
        }
        p = next;
    }
}

void a7r3_maps_reload(void)
{
    g_rng_loaded = 0;
    g_rng_n = 0;
    pm_load_maps();
}

unsigned long a7r3_mod_base(const char *needle)
{
    static char buf[131072];
    int fd, n = 0;
    char *p, *end;
    unsigned long best = 0;

    fd = a7r3_open("/proc/self/maps", A7R3_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    while (n < (int)sizeof(buf) - 1) {
        long r = a7r3_read(fd, buf + n, sizeof(buf) - 1u - (size_t)n);
        if (r <= 0)
            break;
        n += (int)r;
    }
    a7r3_close(fd);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    p = buf;
    end = buf + n;
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
    return best;
}

int a7r3_pm_readable(const void *p, unsigned len)
{
    unsigned long a = (unsigned long)p;
    unsigned long b = a + len;
    int i;
    if (!len || b <= a)
        return 0;
    if (!g_rng_loaded)
        pm_load_maps();
    for (i = 0; i < g_rng_n; i++) {
        if (a >= g_rng[i].lo && b <= g_rng[i].hi)
            return 1;
    }
    return 0;
}

int a7r3_pm_writable(const void *p, unsigned len)
{
    unsigned long a = (unsigned long)p;
    unsigned long b = a + len;
    int i;
    if (!len || b <= a)
        return 0;
    if (!g_rng_loaded)
        pm_load_maps();
    for (i = 0; i < g_rng_n; i++) {
        if (g_rng[i].writable && a >= g_rng[i].lo && b <= g_rng[i].hi)
            return 1;
    }
    return 0;
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
