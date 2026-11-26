#ifndef A7R3_SYS_H
#define A7R3_SYS_H

#include <stddef.h>
#include <stdint.h>

/* ARM Linux EABI syscall numbers */
#define A7R3_SYS_READ          3
#define A7R3_SYS_WRITE         4
#define A7R3_SYS_OPEN          5
#define A7R3_SYS_CLOSE         6
#define A7R3_SYS_UNLINK        10
#define A7R3_SYS_MMAP2         192
#define A7R3_SYS_MUNMAP        91
#define A7R3_SYS_NANOSLEEP     162
#define A7R3_SYS_CLOCK_GETTIME 263
#define A7R3_SYS_FUTEX         240

#define A7R3_FUTEX_WAIT_PRIV   128
#define A7R3_FUTEX_WAKE_PRIV   129

#define A7R3_O_RDONLY          0
#define A7R3_O_WRONLY          1
#define A7R3_O_RDWR            2
#define A7R3_O_CREAT           0100
#define A7R3_O_TRUNC           01000
#define A7R3_O_APPEND          02000
#define A7R3_O_SYNC            0101000  /* O_DSYNC|__O_SYNC on ARM Linux 3.0 */
#define A7R3_O_RDWR_SYNC       (A7R3_O_RDWR | A7R3_O_SYNC)

#define A7R3_PROT_READ         1
#define A7R3_PROT_WRITE        2
#define A7R3_MAP_SHARED        1

#define A7R3_CLOCK_MONOTONIC   1

struct a7r3_timespec32 {
    int tv_sec;
    int tv_nsec;
};

long a7r3_sys2(long nr, long a1, long a2);
long a7r3_sys3(long nr, long a1, long a2, long a3);
long a7r3_sys4(long nr, long a1, long a2, long a3, long a4, long a5, long a6);

int  a7r3_open(const char *path, int flags, int mode);
int  a7r3_close(int fd);
long a7r3_read(int fd, void *buf, size_t n);
long a7r3_write(int fd, const void *buf, size_t n);
void a7r3_unlink(const char *path);
int  a7r3_file_exists(const char *path);

void *a7r3_mmap2(void *addr, size_t len, int prot, int flags, int fd, unsigned long pgoff);
int   a7r3_munmap(void *addr, size_t len);
void  a7r3_nanosleep_ms(unsigned ms);
unsigned a7r3_now_ms(void);
void  a7r3_futex_wait(volatile unsigned *addr, unsigned expected, unsigned timeout_ms);
void  a7r3_futex_wake(volatile unsigned *addr);

static inline void a7r3_dsb(void)
{
    __asm__ volatile("dsb" ::: "memory");
}

/* Process verification and /proc/self/maps safety table */
int  a7r3_comm_is_im(void);
void a7r3_maps_reload(void);
unsigned long a7r3_mod_base(const char *needle);
int  a7r3_pm_readable(const void *p, unsigned len);
int  a7r3_pm_writable(const void *p, unsigned len);

/* Simple freestanding logger -> /tmp_bt/... (no stdio/libc PLT calls) */
void a7r3_log_init(const char *path);
void a7r3_log_str(const char *s);
void a7r3_log_hex(const char *prefix, unsigned long v);

int a7r3_bytes_eq(const char *a, const char *b, unsigned n);
int a7r3_str_has(const char *s, const char *needle);

#endif /* A7R3_SYS_H */
