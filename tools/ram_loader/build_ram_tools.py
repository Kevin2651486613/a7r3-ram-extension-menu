#!/usr/bin/env python3
"""Build pure-RAM ARM ELF helpers for live A7R3 probing and dlopen injection:
  - build_fb_probe_elf(): read-only /dev/mem sampler for 0x202E0200 / 0x23A80000 / 0x23B16000
  - build_remote_svc_elf(): run mmap2 / munmap / cacheflush in target pid via stock SVC
  - build_dlopen_stub_bytes(): 48B+ ARM trampoline calling dlopen(path, RTLD_NOW) + dlerror()
  - build_remote_dlopen_elf(): ptrace executor for the remote dlopen stub (safe reg restore)
"""
import os
import shutil
import struct
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent.parent
SDK_DIR = TOOLS_DIR.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
from env_config import load_local_env  # noqa: E402

load_local_env(SDK_DIR)

HUD_DIR_ENV = os.environ.get("A7R3_HUD_DIR", "").strip()
HUD_DIR = Path(HUD_DIR_ENV) if HUD_DIR_ENV else None

if env_llvm := os.environ.get("A7R3_LLVM_BIN", "").strip():
    LLVM_BIN = Path(env_llvm)
else:
    _clang_in_path = shutil.which("clang")
    LLVM_BIN = Path(_clang_in_path).resolve().parent if _clang_in_path else Path()

CLANG = LLVM_BIN / ("clang.exe" if (LLVM_BIN / "clang.exe").exists() else "clang")
LLD = LLVM_BIN / ("ld.lld.exe" if (LLVM_BIN / "ld.lld.exe").exists() else "ld.lld")

if HUD_DIR is not None:
    _DEPS_DIR = (HUD_DIR / "scratch/precapture/python_deps").resolve()
    if _DEPS_DIR.exists() and str(_DEPS_DIR) not in sys.path:
        sys.path.insert(0, str(_DEPS_DIR))
try:
    from keystone import Ks, KS_ARCH_ARM, KS_MODE_ARM
except ImportError as exc:
    raise ImportError(
        "Missing Python package 'keystone-engine' (keystone). "
        "Install with `pip install keystone-engine` or set A7R3_HUD_DIR in .env.local."
    ) from exc

ENTRY = 0x10100
PAGE = 4096


def _wrap_arm_elf(code: bytes, entry: int = ENTRY) -> bytes:
    header = bytearray(256)
    header[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH", header, 16, 2, 40, 1, entry, 52, 0, 0x5000000, 52, 32, 1, 0, 0, 0
    )
    struct.pack_into("<8I", header, 52, 1, 256, entry, entry, len(code), len(code), 7, 4096)
    return bytes(header) + code


def build_fb_probe_elf(out_dir: Path) -> Path:
    """Compile a freestanding, zero-libc static ARM ELF that reads the three
    panel/present buffers twice (200 ms apart) via /dev/mem O_RDONLY and prints
    non-zero RGB pixels, checksums, and frame-to-frame diff counts.
    """
    src_c = out_dir / "fb_probe.c"
    out_elf = out_dir / "fb_probe.elf"
    src_c.write_text(
        r"""typedef unsigned int u32;
typedef unsigned short u16;

#define SYS_exit      1
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_munmap    91
#define SYS_nanosleep 162
#define SYS_mmap2     192

struct timespec32 {
    long tv_sec;
    long tv_nsec;
};

static long sys1(long nr, long a)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long sys2(long nr, long a, long b)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

static long sys3(long nr, long a, long b, long c)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static long sys6(long nr, long a, long b, long c, long d, long e, long f)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
}

static void puts_raw(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    sys3(SYS_write, 1, (long)s, n);
}

static void put_hex(u32 v)
{
    char b[11];
    int i;
    b[0] = '0';
    b[1] = 'x';
    for (i = 0; i < 8; i++) {
        b[9 - i] = "0123456789abcdef"[v & 0xf];
        v >>= 4;
    }
    b[10] = 0;
    puts_raw(b);
}

static void put_dec(u32 v)
{
    char b[16];
    int n = 0;
    if (v == 0) {
        puts_raw("0");
        return;
    }
    while (v && n < 15) {
        u32 q = v / 10u;
        b[n++] = (char)('0' + (v - q * 10u));
        v = q;
    }
    while (n > 0) {
        char c = b[--n];
        sys3(SYS_write, 1, (long)&c, 1);
    }
}

#define PIX_CNT (640u * 480u)
static u16 g_snap[3][PIX_CNT];

void _start(void)
{
    static const u32 pas[3] = { 0x202e0200u, 0x23a80000u, 0x23b16000u };
    static const char *names[3] = { "render ", "flipA  ", "flipB  " };
    struct timespec32 ts;
    int fd, k;
    u32 i;

    fd = (int)sys3(SYS_open, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0);
    if (fd < 0) {
        puts_raw("FB_PROBE open_err\n");
        sys1(SYS_exit, 1);
    }

    for (k = 0; k < 3; k++) {
        u32 pa = pas[k];
        u32 base = pa & ~0xfffu;
        u32 off = pa & 0xfffu;
        long m = sys6(SYS_mmap2, 0, 0x98000, 1 /* PROT_READ */, 1 /* MAP_SHARED */,
                      fd, (long)(base >> 12));
        if (m < 0 && m > -4096) {
            puts_raw(names[k]);
            puts_raw(" mmap1_err\n");
            continue;
        }
        const volatile u16 *fb = (const volatile u16 *)(m + off);
        for (i = 0; i < PIX_CNT; i++)
            g_snap[k][i] = fb[i];
        sys2(SYS_munmap, m, 0x98000);
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 200000000;
    sys2(SYS_nanosleep, (long)&ts, 0);

    for (k = 0; k < 3; k++) {
        u32 pa = pas[k];
        u32 base = pa & ~0xfffu;
        u32 off = pa & 0xfffu;
        u32 nz1 = 0, nz2 = 0, sum1 = 0, sum2 = 0, diff = 0;
        long m = sys6(SYS_mmap2, 0, 0x98000, 1, 1, fd, (long)(base >> 12));
        if (m < 0 && m > -4096) {
            puts_raw(names[k]);
            puts_raw(" mmap2_err\n");
            continue;
        }
        const volatile u16 *fb = (const volatile u16 *)(m + off);
        for (i = 0; i < PIX_CNT; i++) {
            u16 p1 = g_snap[k][i];
            u16 p2 = fb[i];
            if (p1 & 0xfff0u) nz1++;
            if (p2 & 0xfff0u) nz2++;
            sum1 = sum1 * 131u + p1;
            sum2 = sum2 * 131u + p2;
            if (p1 != p2) diff++;
        }
        sys2(SYS_munmap, m, 0x98000);
        puts_raw(names[k]);
        put_hex(pa);
        puts_raw(" nz1=");
        put_dec(nz1);
        puts_raw(" nz2=");
        put_dec(nz2);
        puts_raw(" diff=");
        put_dec(diff);
        puts_raw(" s1=");
        put_hex(sum1);
        puts_raw(" s2=");
        put_hex(sum2);
        puts_raw("\n");
    }

    sys1(SYS_close, fd);
    sys1(SYS_exit, 0);
}
""",
        encoding="utf-8",
    )

    import subprocess

    cmd = [
        str(CLANG),
        "--target=arm-linux-gnueabi",
        "-march=armv7-a",
        "-mfloat-abi=soft",
        "-static",
        "-O2",
        f"-fuse-ld={LLD}",
        "-nostdlib",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-e",
        "_start",
        "-o",
        str(out_elf),
        str(src_c),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"fb_probe compile failed:\n{res.stderr}")
    return out_elf


def build_fb_dump_elf(out_dir: Path) -> Path:
    """Compile a static ARM ELF: fb_dump <hex_pa> <out_path>
    Maps 640*480*2 bytes via /dev/mem (O_RDONLY | O_SYNC) and writes to <out_path>.
    """
    src_c = out_dir / "fb_dump.c"
    out_elf = out_dir / "fb_dump.elf"
    src_c.write_text(
        r"""typedef unsigned int u32;
typedef unsigned short u16;

#define SYS_exit   1
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_munmap 91
#define SYS_mmap2  192

static long sys1(long nr, long a)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long sys2(long nr, long a, long b)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1) : "memory");
    return r0;
}

static long sys3(long nr, long a, long b, long c)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return r0;
}

static long sys6(long nr, long a, long b, long c, long d, long e, long f)
{
    register long r7 __asm__("r7") = nr;
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r3 __asm__("r3") = d;
    register long r4 __asm__("r4") = e;
    register long r5 __asm__("r5") = f;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
                     : "memory");
    return r0;
}

static u32 parse_hex(const char *s)
{
    u32 v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    while (*s) {
        char c = *s++;
        u32 d;
        if (c >= '0' && c <= '9') d = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (u32)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}

static u16 g_buf[640u * 480u];

void _start(void) __attribute__((naked));
void main_c(int argc, char **argv);

void _start(void)
{
    __asm__ volatile(
        "ldr r0, [sp]\n"
        "add r1, sp, #4\n"
        "bl main_c\n"
        "mov r7, #1\n"
        "svc #0\n"
    );
}

void main_c(int argc, char **argv)
{
    u32 pa, base, off, i, total = 640u * 480u * 2u;
    int mfd, ofd;
    long m;
    const volatile u16 *fb;
    u32 done = 0;

    if (argc < 3)
        sys1(SYS_exit, 1);

    pa = parse_hex(argv[1]);
    base = pa & ~0xfffu;
    off = pa & 0xfffu;

    mfd = (int)sys3(SYS_open, (long)"/dev/mem", 0x101000 /* O_RDONLY | O_SYNC */, 0);
    if (mfd < 0)
        sys1(SYS_exit, 2);

    m = sys6(SYS_mmap2, 0, 0x98000, 1, 1, mfd, (long)(base >> 12));
    if (m < 0 && m > -4096)
        sys1(SYS_exit, 3);

    fb = (const volatile u16 *)(m + off);
    for (i = 0; i < 640u * 480u; i++)
        g_buf[i] = fb[i];

    sys2(SYS_munmap, m, 0x98000);
    sys1(SYS_close, mfd);

    ofd = (int)sys3(SYS_open, (long)argv[2], 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0644);
    if (ofd < 0)
        sys1(SYS_exit, 4);
    while (done < total) {
        long w = sys3(SYS_write, ofd, (long)((char *)g_buf + done), (long)(total - done));
        if (w <= 0)
            sys1(SYS_exit, 5);
        done += (u32)w;
    }
    sys1(SYS_close, ofd);
    sys1(SYS_exit, 0);
}
""",
        encoding="utf-8",
    )

    import subprocess

    cmd = [
        str(CLANG),
        "--target=arm-linux-gnueabi",
        "-march=armv7-a",
        "-mfloat-abi=soft",
        "-static",
        "-O2",
        f"-fuse-ld={LLD}",
        "-nostdlib",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-e",
        "_start",
        "-o",
        str(out_elf),
        str(src_c),
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"fb_dump compile failed:\n{res.stderr}")
    return out_elf


def build_remote_svc_elf(pid: int, svc: int, nr: int, args: tuple[int, ...]) -> bytes:
    """Execute one syscall (mmap2=192, munmap=91, cacheflush=0xf0002) inside `pid`
    at an existing verified ARM `svc #0` word without modifying target code.
    """
    if not 2 <= pid < 32768 or not 0x10000 <= svc < 0x7FFFFFFC or svc % 4:
        raise ValueError("Invalid PID or ARM SVC address")
    if nr not in (91, 192, 0xF0002):
        raise ValueError(f"Unsupported syscall {nr}")
    if len(args) > 6:
        raise ValueError("Too many syscall args")

    setup = "\n".join(f"ldr r0, ={val}\nstr r0, [r5, #{i * 4}]" for i, val in enumerate(args))

    def trace(req: int, addr: str = "#0", data: str = "#0") -> str:
        return f"""mov r0, #{req}
            movw r1, #{pid}
            mov r2, {addr}
            mov r3, {data}
            mov r7, #26
            svc #0
            cmp r0, #0"""

    get_r5 = trace(12, data="r5")
    opts = trace(0, data="#1").replace("mov r0, #0", "movw r0, #0x4200")
    src = f"""
    .arm
        sub sp, sp, #512
        add r4, sp, #32
        add r5, sp, #128
        mov r0, #1
        ldr r1, =sighup_ignore
        mov r2, #0
        mov r3, #8
        mov r7, #174
        svc #0
        cmp r0, #0
        bne attach_error
        {trace(16)}
        bne attach_error
        bl wait_stopped
        cmp r0, #19
        bne initial_signal
        mov r8, #21
        {trace(12, data='r4')}
        bne detach_only
        ldr r0, [r4, #64]
        and r0, r0, #31
        cmp r0, #16
        bne detach_only
        ldr r6, ={svc}
        add r11, sp, #256
        {trace(1, addr='r6', data='r11')}
        bne detach_only
        ldr r0, [r11]
        ldr r1, =0xef000000
        cmp r0, r1
        bne detach_only
        {opts}
        bne detach_only
        mov r0, #0
    copy:
        ldr r1, [r4, r0]
        str r1, [r5, r0]
        add r0, r0, #4
        cmp r0, #72
        bne copy
        {setup}
        ldr r0, ={nr}
        str r0, [r5, #28]
        str r6, [r5, #60]
        ldr r0, [r5, #64]
        ldr r1, =0x0600fc20
        bic r0, r0, r1
        str r0, [r5, #64]
        mvn r0, #0
        str r0, [r5, #68]
        {trace(13, data='r5')}
        bne restore
        mov r8, #22
        {trace(24)}
        bne restore
        bl wait_stopped
        cmp r0, #133
        bne restore
        {get_r5}
        bne restore
        bl verify_boundary
        bne restore
        mov r8, #23
        {trace(24)}
        bne restore
        bl wait_stopped
        cmp r0, #133
        bne restore
        {get_r5}
        bne restore
        bl verify_boundary
        bne restore
        ldr r9, [r5]
        mov r8, #0
    restore:
        {trace(13, data='r4')}
        bne recovery_failed
        {get_r5}
        bne recovery_failed
        mov r0, #0
    actual_regs:
        ldr r1, [r4, r0]
        ldr r2, [r5, r0]
        cmp r1, r2
        bne recovery_failed
        add r0, r0, #4
        cmp r0, #72
        bne actual_regs
    detach_only:
        {trace(17)}
        bne recovery_failed
        cmp r8, #0
        bne finish
        ldr r1, =out
        mov r2, #8
    hex_out:
        mov r0, r9, lsr #28
        cmp r0, #10
        addlo r0, r0, #48
        addhs r0, r0, #87
        strb r0, [r1], #1
        lsl r9, r9, #4
        subs r2, r2, #1
        bne hex_out
        mov r0, #1
        ldr r1, =out
        mov r2, #9
        mov r7, #4
        svc #0
        cmp r0, #9
        movne r8, #24
        b finish
    initial_signal:
        mov r10, r0
        mov r8, #27
        {trace(17, data='r10')}
        bne recovery_failed
        b finish
    attach_error:
        mov r8, #20
        b finish
    verify_boundary:
        ldr r0, [r5, #60]
        add r1, r6, #4
        cmp r0, r1
        bxne lr
        ldr r0, [r5, #28]
        ldr r1, ={nr}
        cmp r0, r1
        bx lr
    wait_stopped:
        mov r11, #60
    wait_poll:
        movw r0, #{pid}
        mov r1, sp
        mov r2, #0x40000001
        mov r3, #0
        mov r7, #114
        svc #0
        movw r1, #{pid}
        cmp r0, r1
        beq wait_got
        cmp r0, #0
        beq wait_tick
        cmn r0, #4
        bne recovery_failed
    wait_tick:
        ldr r0, =tick
        mov r1, #0
        mov r7, #162
        svc #0
        subs r11, r11, #1
        bne wait_poll
        b recovery_failed
    wait_got:
        ldr r0, [sp]
        and r1, r0, #255
        cmp r1, #127
        bne recovery_failed
        mov r0, r0, lsr #8
        and r0, r0, #255
        bx lr
    recovery_failed:
        mov r7, #20
        svc #0
        mov r1, #19
        mov r7, #37
        svc #0
        b recovery_failed
    finish:
        mov r0, r8
        mov r7, #1
        svc #0
        .ltorg
    tick: .word 0, 20000000
    sighup_ignore: .word 1, 0, 0, 0, 0
    out: .ascii "00000000\\n"
    """
    code = bytes(Ks(KS_ARCH_ARM, KS_MODE_ARM).asm(src, ENTRY)[0])
    return _wrap_arm_elf(code)


def build_dlopen_stub_bytes(
    stub_addr: int,
    dlopen_addr: int,
    dlerror_addr: int,
    path_str: str,
) -> tuple[bytes, int, int, int]:
    """Build ARM stub placed at `stub_addr` that calls dlopen(path, RTLD_NOW=2).
    Stores handle at `stub_addr + 0x200` and dlerror() ptr at `stub_addr + 0x204`,
    then executes `bkpt #0` (0xe1200070).
    Returns (blob, path_addr, res_addr, trap_addr).
    """
    path_addr = stub_addr + 0x100
    res_addr = stub_addr + 0x200

    src = f"""
    .arm
        ldr r0, =0x{path_addr:08x}
        mov r1, #2
        ldr r3, =0x{dlopen_addr:08x}
        blx r3
        ldr r4, =0x{res_addr:08x}
        str r0, [r4, #0]
        mov r1, #0
        str r1, [r4, #4]
        cmp r0, #0
        bne trap_here
        ldr r3, =0x{dlerror_addr:08x}
        blx r3
        str r0, [r4, #4]
    trap_here:
        bkpt #0
        .ltorg
    """
    ks = Ks(KS_ARCH_ARM, KS_MODE_ARM)
    code = bytes(ks.asm(src, stub_addr)[0])
    bkpt_off = code.find(struct.pack("<I", 0xE1200070))
    if bkpt_off < 0 or bkpt_off % 4 != 0:
        raise RuntimeError("Could not locate bkpt #0 in dlopen stub")
    if len(code) > 0x100:
        raise RuntimeError(f"dlopen stub too large: {len(code)}")
    path_bytes = path_str.encode("ascii") + b"\x00"
    if len(path_bytes) > 0x100:
        raise RuntimeError("path string too long")

    blob = bytearray(0x210)
    blob[0 : len(code)] = code
    blob[0x100 : 0x100 + len(path_bytes)] = path_bytes
    return bytes(blob), path_addr, res_addr, stub_addr + bkpt_off


def build_remote_dlopen_elf(pid: int, stub_addr: int, stack_top: int, trap_addr: int) -> bytes:
    """Ptrace-attach `pid`, redirect stopped thread to `stub_addr` with private
    8-byte aligned `stack_top`, wait for `bkpt #0` at `trap_addr`, restore all
    18 registers cleanly, and detach.
    Exit codes:
      0: trap hit at trap_addr and registers restored
      20: ptrace attach failed
      21: initial stop or GETREGS failed / not USR mode
      22: SETREGS/CONT failed
      25: stopped on unexpected signal or wrong PC (registers still restored)
    """
    def trace(req: int, addr: str = "#0", data: str = "#0") -> str:
        return f"""mov r0, #{req}
            movw r1, #{pid}
            mov r2, {addr}
            mov r3, {data}
            mov r7, #26
            svc #0
            cmp r0, #0"""

    get_r5 = trace(12, data="r5")
    src = f"""
    .arm
        sub sp, sp, #512
        add r4, sp, #32
        add r5, sp, #128
        mov r0, #1
        ldr r1, =sighup_ignore
        mov r2, #0
        mov r3, #8
        mov r7, #174
        svc #0
        cmp r0, #0
        bne attach_error
        {trace(16)}
        bne attach_error
        bl wait_stopped
        cmp r0, #19
        bne initial_signal
        mov r8, #21
        {trace(12, data='r4')}
        bne detach_only
        ldr r0, [r4, #64]
        and r0, r0, #31
        cmp r0, #16
        bne detach_only
        mov r0, #0
    copy:
        ldr r1, [r4, r0]
        str r1, [r5, r0]
        add r0, r0, #4
        cmp r0, #72
        bne copy
        ldr r0, =0x{stack_top:08x}
        str r0, [r5, #52] /* sp */
        ldr r0, =0x{stub_addr:08x}
        str r0, [r5, #60] /* pc */
        ldr r0, [r5, #64]
        ldr r1, =0x0600fc20
        bic r0, r0, r1
        str r0, [r5, #64] /* cpsr (ARM mode) */
        mvn r0, #0
        str r0, [r5, #68] /* orig_r0 */
        mov r8, #22
        {trace(13, data='r5')}
        bne restore
        {trace(7)} /* PTRACE_CONT */
        bne restore
        bl wait_stopped
        mov r9, r0
        {get_r5}
        bne restore
        cmp r9, #5 /* SIGTRAP */
        bne unexpected_stop
        ldr r0, [r5, #60]
        ldr r1, =0x{trap_addr:08x}
        cmp r0, r1
        bne unexpected_stop
        mov r8, #0
        b restore
    unexpected_stop:
        mov r8, #25
    restore:
        {trace(13, data='r4')}
        bne recovery_failed
        {get_r5}
        bne recovery_failed
        mov r0, #0
    actual_regs:
        ldr r1, [r4, r0]
        ldr r2, [r5, r0]
        cmp r1, r2
        bne recovery_failed
        add r0, r0, #4
        cmp r0, #72
        bne actual_regs
    detach_only:
        {trace(17)}
        bne recovery_failed
        b finish
    initial_signal:
        mov r10, r0
        mov r8, #21
        {trace(17, data='r10')}
        bne recovery_failed
        b finish
    attach_error:
        mov r8, #20
        b finish
    wait_stopped:
        mov r11, #250
    wait_poll:
        movw r0, #{pid}
        mov r1, sp
        mov r2, #0x40000001
        mov r3, #0
        mov r7, #114
        svc #0
        movw r1, #{pid}
        cmp r0, r1
        beq wait_got
        cmp r0, #0
        beq wait_tick
        cmn r0, #4
        bne recovery_failed
    wait_tick:
        ldr r0, =tick
        mov r1, #0
        mov r7, #162
        svc #0
        subs r11, r11, #1
        bne wait_poll
        b recovery_failed
    wait_got:
        ldr r0, [sp]
        and r1, r0, #255
        cmp r1, #127
        bne recovery_failed
        mov r0, r0, lsr #8
        and r0, r0, #255
        bx lr
    recovery_failed:
        mov r7, #20
        svc #0
        mov r1, #19
        mov r7, #37
        svc #0
        b recovery_failed
    finish:
        mov r0, r8
        mov r7, #1
        svc #0
        .ltorg
    tick: .word 0, 20000000
    sighup_ignore: .word 1, 0, 0, 0, 0
    """
    code = bytes(Ks(KS_ARCH_ARM, KS_MODE_ARM).asm(src, ENTRY)[0])
    return _wrap_arm_elf(code)


def main() -> int:
    out_dir = Path(__file__).resolve().parent / "gen"
    out_dir.mkdir(parents=True, exist_ok=True)
    fb = build_fb_probe_elf(out_dir)
    svc = build_remote_svc_elf(158, 0x042DD000, 192, (0, 0x10000, 7, 0x22, 0xFFFFFFFF, 0))
    stub, p_addr, r_addr, t_addr = build_dlopen_stub_bytes(
        0x15B00000, 0x0420DBC9, 0x0420DE05, "/tmp_bt/newmenu_ram.so"
    )
    dl = build_remote_dlopen_elf(158, 0x15B00000, 0x15B0FFF0, t_addr)
    print(f"[+] fb_probe.elf: {fb.stat().st_size} B")
    print(f"[+] remote_svc.elf sample: {len(svc)} B")
    print(
        f"[+] dlopen stub: {len(stub)} B (path={p_addr:#x}, res={r_addr:#x}, trap={t_addr:#x})"
    )
    print(f"[+] remote_dlopen.elf sample: {len(dl)} B")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
