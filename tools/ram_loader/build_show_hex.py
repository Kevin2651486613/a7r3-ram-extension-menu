#!/usr/bin/env python3
"""MIT clean-room ARM32 static ELF builder for streaming hex memory/file reads."""
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


def build_show_hex_elf(source_path: str = "/proc/self/mem", max_bytes: int = 256) -> bytes:
    """Build a static ARM32 ELF tool: <prog> <hex_addr> <dec_or_hex_len>.

    Reads `len` bytes (`1 <= len <= max_bytes`) from `source_path` at unsigned 32-bit
    byte offset `addr` via SYS__llseek (140) and prints lowercase hex to stdout.
    """
    if not isinstance(source_path, str) or not source_path or "\x00" in source_path:
        raise ValueError(f"Invalid source_path: {source_path!r}")
    path_bytes = source_path.encode("ascii") + b"\x00"
    if len(path_bytes) > 256:
        raise ValueError("source_path too long (>255 bytes)")
    if not isinstance(max_bytes, int) or max_bytes < 1 or max_bytes > 1048576:
        raise ValueError(f"Invalid max_bytes: {max_bytes!r} (expected 1..1048576)")

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
        mov r4, r0

        ldr r0, [sp, #12]
        bl parse_len_arg
        cmp r1, #0
        movne r0, #2
        bne exit_now
        cmp r0, #0
        moveq r0, #2
        beq exit_now
        ldr r1, ={max_bytes}
        cmp r0, r1
        movhi r0, #2
        bhi exit_now
        mov r5, r0

        sub sp, sp, #800

        ldr r0, =source_path
        mov r1, #0
        mov r2, #0
        mov r7, #5
        svc #0
        cmp r0, #0
        movlt r0, #3
        blt exit_now
        mov r6, r0

        mov r0, r6
        mov r1, #0
        mov r2, r4
        mov r3, sp
        mov r4, #0
        mov r7, #140
        svc #0
        cmp r0, #0
        bne err_close4

    stream_loop:
        cmp r5, #0
        beq done_ok
        cmp r5, #256
        movls r8, r5
        movhi r8, #256

        mov r9, #0
    read_loop:
        cmp r9, r8
        bge read_done
        mov r0, r6
        add r1, sp, #16
        add r1, r1, r9
        sub r2, r8, r9
        mov r7, #3
        svc #0
        cmp r0, #0
        bgt read_adv
        beq err_close5
        cmn r0, #4
        beq read_loop
        b err_close5
    read_adv:
        add r9, r9, r0
        b read_loop

    read_done:
        add r0, sp, #16
        add r1, sp, #272
        mov r2, r8
    hex_loop:
        ldrb r3, [r0], #1
        mov r4, r3, lsr #4
        cmp r4, #10
        addlo r4, r4, #48
        addhs r4, r4, #87
        strb r4, [r1], #1
        and r4, r3, #15
        cmp r4, #10
        addlo r4, r4, #48
        addhs r4, r4, #87
        strb r4, [r1], #1
        subs r2, r2, #1
        bne hex_loop

        sub r5, r5, r8
        lsl r9, r8, #1
        cmp r5, #0
        bne write_start
        mov r4, #10
        strb r4, [r1], #1
        add r9, r9, #1

    write_start:
        mov r10, #0
    write_loop:
        cmp r10, r9
        bge stream_loop
        mov r0, #1
        add r1, sp, #272
        add r1, r1, r10
        sub r2, r9, r10
        mov r7, #4
        svc #0
        cmp r0, #0
        bgt write_adv
        cmn r0, #4
        beq write_loop
        b err_close6
    write_adv:
        add r10, r10, r0
        b write_loop

    done_ok:
        mov r0, r6
        mov r7, #6
        svc #0
        mov r0, #0
        b exit_now

    err_close4:
        mov r4, #4
        b err_close_common
    err_close5:
        mov r4, #5
        b err_close_common
    err_close6:
        mov r4, #6
    err_close_common:
        mov r0, r6
        mov r7, #6
        svc #0
        mov r0, r4
    exit_now:
        mov r7, #1
        svc #0

    parse_len_arg:
        ldrb r1, [r0]
        cmp r1, #48
        bne parse_dec_start
        ldrb r2, [r0, #1]
        cmp r2, #120
        cmpne r2, #88
        beq parse_hex_arg
    parse_dec_start:
        mov r2, #0
        mov r3, #0
        mov r12, #10
    parse_dec_loop:
        ldrb r1, [r0], #1
        cmp r1, #0
        beq parse_done
        cmp r1, #48
        blt parse_err
        cmp r1, #57
        bgt parse_err
        sub r1, r1, #48
        mul r2, r12, r2
        add r2, r2, r1
        mov r3, #1
        b parse_dec_loop

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
    source_path:
        .byte {path_lit}
        .balign 4
    """
    code = bytes(Ks(KS_ARCH_ARM, KS_MODE_ARM).asm(src, ENTRY)[0])
    return wrap_arm_elf(code, ENTRY)


if __name__ == "__main__":
    elf = build_show_hex_elf()
    print(f"[+] sdk_show_hex sample ELF: {len(elf)} bytes")
