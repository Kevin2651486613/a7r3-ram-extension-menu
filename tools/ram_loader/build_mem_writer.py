#!/usr/bin/env python3
"""MIT clean-room ARM32 static ELF builder for target process memory writes."""
from __future__ import annotations

import os
import sys
from pathlib import Path

try:
    from ram_loader import DEFAULT_ENTRY as ENTRY, wrap_arm_elf
except ImportError:
    _THIS_DIR = Path(__file__).resolve().parent
    if str(_THIS_DIR) not in sys.path:
        sys.path.insert(0, str(_THIS_DIR))
    from __init__ import DEFAULT_ENTRY as ENTRY, wrap_arm_elf  # type: ignore[no-redef]

_HUD_DIR_ENV = os.environ.get("A7R3_HUD_DIR", "").strip()
if _HUD_DIR_ENV:
    _LEGACY_DEPS = (Path(_HUD_DIR_ENV) / "scratch/precapture/python_deps").resolve()
    if _LEGACY_DEPS.exists() and str(_LEGACY_DEPS) not in sys.path:
        sys.path.insert(0, str(_LEGACY_DEPS))

try:
    from keystone import KS_ARCH_ARM, KS_MODE_ARM, Ks
except ImportError as exc:
    raise ImportError(
        "Missing Python package 'keystone-engine' (keystone). "
        "Install with `pip install -r requirements.txt`."
    ) from exc


def build_mem_writer_elf(pid: int = 158, mem_path: str | None = None) -> bytes:
    """Build a static ARM32 ELF tool: <prog> <hex_addr> <input_file_path>.

    Primary path writes directly to `/proc/<pid>/mem` (or explicit `mem_path`)
    via SYS__llseek (140) + streaming SYS_write. If the kernel denies /proc/pid/mem
    writes (-EPERM/-EIO/-EACCES/-EINVAL), automatically falls back to
    ptrace(PTRACE_ATTACH / PEEKDATA / POKEDATA / DETACH).
    """
    if not isinstance(pid, int) or not 2 <= pid < 32768:
        raise ValueError(f"Invalid pid: {pid!r} (expected 2..32767)")

    target_path = mem_path if mem_path is not None else f"/proc/{pid}/mem"
    if not isinstance(target_path, str) or not target_path or "\x00" in target_path:
        raise ValueError(f"Invalid mem_path: {target_path!r}")
    path_bytes = target_path.encode("ascii") + b"\x00"
    if len(path_bytes) > 256:
        raise ValueError("mem_path too long (>255 bytes)")

    path_lit = ",".join(str(b) for b in path_bytes)
    src = f"""
    .arm
    _start:
        ldr r4, [sp]
        cmp r4, #3
        movlt r0, #1
        blt exit_now

        ldr r0, [sp, #8]
        bl parse_hex_arg
        cmp r1, #0
        movne r0, #2
        bne exit_now
        mov r10, r0             @ r10 = target virtual addr (u32)

        ldr r4, [sp, #12]       @ r4 = input file path ptr
        cmp r4, #0
        moveq r0, #2
        beq exit_now

        sub sp, sp, #576        @ [0..7]=loff_t, [8..11]=ptrace_word/status, [16..527]=io[512]

        mov r0, r4
        mov r1, #0              @ O_RDONLY
        mov r2, #0
        mov r7, #5              @ SYS_open
        svc #0
        cmp r0, #0
        movlt r0, #3
        blt exit_now
        mov r6, r0              @ r6 = in_fd

        ldr r0, =mem_path
        mov r1, #1              @ O_WRONLY
        mov r2, #0
        mov r7, #5
        svc #0
        cmp r0, #0
        blt check_open_fallback
        mov r8, r0              @ r8 = mem_fd

        mov r0, r8
        mov r1, #0
        mov r2, r10
        mov r3, sp
        mov r4, #0              @ SEEK_SET
        mov r7, #140            @ SYS__llseek
        svc #0
        cmp r0, #0
        bne err_close_both5

        mov r11, #0             @ r11 = total written so far
    dir_chunk_loop:
        mov r0, r6
        add r1, sp, #16
        mov r2, #512
        mov r7, #3              @ SYS_read
        svc #0
        cmp r0, #0
        beq dir_done_ok         @ EOF
        bgt dir_got_bytes
        cmn r0, #4              @ -EINTR
        beq dir_chunk_loop
        b err_close_both6

    dir_got_bytes:
        mov r9, r0              @ r9 = bytes in buffer
        mov r5, #0              @ r5 = offset inside buffer
    dir_write_loop:
        cmp r5, r9
        bge dir_chunk_loop
        mov r0, r8
        add r1, sp, #16
        add r1, r1, r5
        sub r2, r9, r5
        mov r7, #4              @ SYS_write
        svc #0
        cmp r0, #0
        bgt dir_write_adv
        cmn r0, #4              @ -EINTR
        beq dir_write_loop
        cmp r11, #0
        bne err_close_both7
        @ Retry via ptrace only if initial write hits -EPERM/-EIO/-EACCES/-EINVAL
        cmn r0, #1
        cmnne r0, #5
        cmnne r0, #13
        cmnne r0, #22
        bne err_close_both7

        mov r0, r8
        mov r7, #6              @ close mem_fd before fallback
        svc #0
        mov r0, r6
        mov r1, #0
        mov r2, #0
        mov r3, sp
        mov r4, #0
        mov r7, #140            @ rewind in_fd to 0
        svc #0
        cmp r0, #0
        bne err_close_in4
        b ptrace_fallback

    dir_write_adv:
        add r5, r5, r0
        add r11, r11, r0
        b dir_write_loop

    dir_done_ok:
        mov r0, r8
        mov r7, #6
        svc #0
        mov r0, r6
        mov r7, #6
        svc #0
        mov r0, #0
        b exit_now

    check_open_fallback:
        cmn r0, #1
        cmnne r0, #5
        cmnne r0, #13
        cmnne r0, #22
        bne err_close_in4

    ptrace_fallback:
        mov r0, #1              @ SIGHUP
        ldr r1, =sighup_ignore
        mov r2, #0
        mov r3, #8
        mov r7, #174            @ SYS_rt_sigaction
        svc #0

        mov r0, #16             @ PTRACE_ATTACH
        ldr r1, ={pid}
        mov r2, #0
        mov r3, #0
        mov r7, #26             @ SYS_ptrace
        svc #0
        cmp r0, #0
        bne err_close_in8

        mov r11, #60
    pt_wait_loop:
        ldr r0, ={pid}
        add r1, sp, #8
        ldr r2, =0x40000001     @ __WALL | WNOHANG
        mov r3, #0
        mov r7, #114            @ SYS_wait4
        svc #0
        ldr r1, ={pid}
        cmp r0, r1
        beq pt_wait_got
        cmp r0, #0
        beq pt_wait_sleep
        cmn r0, #4              @ -EINTR
        bne pt_err_detach8
    pt_wait_sleep:
        ldr r0, =wait_tick
        mov r1, #0
        mov r7, #162            @ SYS_nanosleep
        svc #0
        subs r11, r11, #1
        bne pt_wait_loop
        b pt_err_detach8

    pt_wait_got:
        ldr r0, [sp, #8]
        and r0, r0, #0xff
        cmp r0, #0x7f
        bne pt_err_detach8

    pt_read_chunk:
        mov r0, r6
        add r1, sp, #16
        mov r2, #512
        mov r7, #3              @ SYS_read
        svc #0
        cmp r0, #0
        beq pt_done_ok
        bgt pt_got_bytes
        cmn r0, #4
        beq pt_read_chunk
        b pt_err_detach6

    pt_got_bytes:
        mov r9, r0              @ r9 = remaining in chunk
        add r5, sp, #16         @ r5 = src ptr

    pt_byte_or_word:
        cmp r9, #0
        beq pt_read_chunk
        tst r10, #3
        bne pt_partial_byte
        cmp r9, #4
        blt pt_partial_byte

        ldrb r3, [r5, #0]
        ldrb r1, [r5, #1]
        orr r3, r3, r1, lsl #8
        ldrb r1, [r5, #2]
        orr r3, r3, r1, lsl #16
        ldrb r1, [r5, #3]
        orr r3, r3, r1, lsl #24
        mov r0, #5              @ PTRACE_POKEDATA
        ldr r1, ={pid}
        mov r2, r10
        mov r7, #26
        svc #0
        cmp r0, #0
        bne pt_err_detach9
        add r10, r10, #4
        add r5, r5, #4
        sub r9, r9, #4
        b pt_byte_or_word

    pt_partial_byte:
        bic r11, r10, #3        @ aligned word addr
        mov r0, #2              @ PTRACE_PEEKDATA
        ldr r1, ={pid}
        mov r2, r11
        add r3, sp, #8
        mov r7, #26
        svc #0
        cmp r0, #0
        bne pt_err_detach9
        ldrb r0, [r5], #1
        and r1, r10, #3
        add r2, sp, #8
        strb r0, [r2, r1]
        ldr r3, [sp, #8]
        mov r0, #5              @ PTRACE_POKEDATA
        ldr r1, ={pid}
        mov r2, r11
        mov r7, #26
        svc #0
        cmp r0, #0
        bne pt_err_detach9
        add r10, r10, #1
        sub r9, r9, #1
        b pt_byte_or_word

    pt_done_ok:
        mov r0, #17             @ PTRACE_DETACH
        ldr r1, ={pid}
        mov r2, #0
        mov r3, #0
        mov r7, #26
        svc #0
        mov r0, r6
        mov r7, #6
        svc #0
        mov r0, #0
        b exit_now

    pt_err_detach6:
        mov r4, #6
        b pt_detach_exit
    pt_err_detach8:
        mov r4, #8
        b pt_detach_exit
    pt_err_detach9:
        mov r4, #9
    pt_detach_exit:
        mov r0, #17
        ldr r1, ={pid}
        mov r2, #0
        mov r3, #0
        mov r7, #26
        svc #0
        mov r0, r6
        mov r7, #6
        svc #0
        mov r0, r4
        b exit_now

    err_close_both5:
        mov r4, #5
        b err_close_both_common
    err_close_both6:
        mov r4, #6
        b err_close_both_common
    err_close_both7:
        mov r4, #7
    err_close_both_common:
        mov r0, r8
        mov r7, #6
        svc #0
        b err_close_in_exit

    err_close_in4:
        mov r4, #4
        b err_close_in_exit
    err_close_in8:
        mov r4, #8
    err_close_in_exit:
        mov r0, r6
        mov r7, #6
        svc #0
        mov r0, r4
    exit_now:
        mov r7, #1
        svc #0

    parse_hex_arg:
        ldrb r1, [r0]
        cmp r1, #48
        bne parse_hex_start
        ldrb r2, [r0, #1]
        cmp r2, #120
        cmpne r2, #88
        addeq r0, r0, #2
    parse_hex_start:
        mov r2, #0
        mov r3, #0
    parse_hex_loop:
        ldrb r1, [r0], #1
        cmp r1, #0
        beq parse_done
        cmp r1, #48
        blt parse_err
        cmp r1, #57
        suble r1, r1, #48
        ble parse_hex_acc
        cmp r1, #65
        blt parse_err
        cmp r1, #70
        suble r1, r1, #55
        ble parse_hex_acc
        cmp r1, #97
        blt parse_err
        cmp r1, #102
        suble r1, r1, #87
        ble parse_hex_acc
        b parse_err
    parse_hex_acc:
        lsl r2, r2, #4
        orr r2, r2, r1
        mov r3, #1
        b parse_hex_loop
    parse_done:
        cmp r3, #0
        beq parse_err
        mov r0, r2
        mov r1, #0
        bx lr
    parse_err:
        mov r0, #0
        mov r1, #1
        bx lr

        .ltorg
    wait_tick:
        .word 0, 20000000
    sighup_ignore:
        .word 1, 0, 0, 0, 0
    mem_path:
        .byte {path_lit}
        .balign 4
    """
    code = bytes(Ks(KS_ARCH_ARM, KS_MODE_ARM).asm(src, ENTRY)[0])
    return wrap_arm_elf(code, ENTRY)


if __name__ == "__main__":
    elf = build_mem_writer_elf()
    print(f"[+] sdk_mem_writer sample ELF: {len(elf)} bytes")
