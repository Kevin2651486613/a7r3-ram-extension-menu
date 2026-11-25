#!/usr/bin/env python3
"""MIT clean-room generator for local a7r3_dual_hud_payload.local.h.

Reads Sony ILCE-7RM3 (A7R3 v3.10) `viewStlrec.uxc` either from a user's
local firmware extraction or read-only from a connected camera over USB,
clones the histogram master descriptor (`0xd1073927`) from layout `0x0200802a`
as `0xd1153927` into level layout `0x02008029`, and writes gitignored
`src/a7r3_dual_hud_payload.local.h`.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent
SRC_DIR = SDK_DIR / "src"
DEFAULT_OUT = SRC_DIR / "a7r3_dual_hud_payload.local.h"

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
from env_config import load_local_env  # noqa: E402

load_local_env(SDK_DIR)

UXC_MAGIC = b"uxc\x07"
SLOT_LEVEL_OUTER = 0x29      # 0x02008029 (DISP Mode 4 level)
SLOT_HIST_OUTER = 0x2A       # 0x0200802a (DISP Mode 5 histogram)
WID_HIST_SRC_MASTER = 0xD1073927
WID_NEW_MASTER = 0xD1153927
EXPECTED_PATCHED_LEN = 2380
CAMERA_UXC_PATH = "/usr/share/app/viewStlrec.uxc"


def find_local_uxc(
    fw_lib_dir: Path | None = None,
    cli_uxc: Path | None = None,
) -> Path | None:
    """Locate `viewStlrec.uxc` from CLI arg, env var, or firmware extraction siblings."""
    candidates: list[Path] = []
    if cli_uxc is not None:
        candidates.append(cli_uxc)
    if env_uxc := os.environ.get("A7R3_UXC_PATH", "").strip():
        candidates.append(Path(env_uxc))

    lib_dir = fw_lib_dir
    if lib_dir is None:
        if env_lib := os.environ.get("A7R3_FW_LIB_DIR", "").strip():
            lib_dir = Path(env_lib)
    if lib_dir is not None:
        candidates.extend(
            [
                lib_dir.parent / "usr" / "share" / "app" / "viewStlrec.uxc",
                lib_dir.parent / "share" / "app" / "viewStlrec.uxc",
                lib_dir.parent.parent / "usr" / "share" / "app" / "viewStlrec.uxc",
                lib_dir / "viewStlrec.uxc",
            ]
        )

    candidates.extend(
        [
            SDK_DIR / "firmware" / "usr" / "share" / "app" / "viewStlrec.uxc",
            SDK_DIR / "extraction" / "files" / "usr" / "share" / "app" / "viewStlrec.uxc",
        ]
    )

    for cand in candidates:
        if cand.is_file():
            return cand.resolve()
    return None


def _extract_uxc_record(uxc_bytes: bytes, slot: int) -> bytes:
    if len(uxc_bytes) < 0x100 or uxc_bytes[:4] != UXC_MAGIC:
        raise ValueError("Invalid viewStlrec.uxc header (expected magic 'uxc\\x07')")
    slot_cnt = uxc_bytes[0x0C]
    if slot < 0 or slot >= slot_cnt:
        raise ValueError(f"Slot {slot:#x} out of range (slot_cnt={slot_cnt})")
    dir_end = 0x10 + slot_cnt * 2
    if dir_end + 6 >= len(uxc_bytes):
        raise ValueError("Truncated UXC directory table")
    rec_base = dir_end + 6
    val = struct.unpack_from("<H", uxc_bytes, 0x10 + slot * 2)[0]
    if val == 0xFFFF:
        raise ValueError(f"Empty UXC slot {slot:#x}")
    off = rec_base + val * 4
    if off + 0x38 > len(uxc_bytes):
        raise ValueError(f"UXC record {slot:#x} header out of bounds ({off:#x})")
    total_len = struct.unpack_from("<I", uxc_bytes, off + 0x20)[0]
    if total_len < 0x40 or off + total_len > len(uxc_bytes):
        raise ValueError(f"Invalid UXC record {slot:#x} length: {total_len}")
    return uxc_bytes[off : off + total_len]


def _parse_record_ptr(rec: bytes) -> tuple[bytearray, int, list[bytes], bytes]:
    if len(rec) < 0x40:
        raise ValueError("RecordPtr data too short")
    tbl = struct.unpack_from("<I", rec, 0x18)[0]
    w_end = struct.unpack_from("<I", rec, 0x1C)[0]
    r_len = struct.unpack_from("<I", rec, 0x20)[0]
    if tbl < 0x38 or w_end <= tbl or r_len > len(rec) or w_end > r_len:
        raise ValueError(f"Corrupt RecordPtr header: tbl={tbl:#x} w_end={w_end:#x} r_len={r_len:#x}")
    cnt = rec[tbl]
    if cnt == 0 or tbl + 1 + cnt * 2 > w_end:
        raise ValueError(f"Invalid widget count {cnt}")
    offs = list(struct.unpack_from(f"<{cnt}H", rec, tbl + 1))
    starts = [tbl + o for o in offs] + [w_end]
    for i in range(cnt):
        if starts[i] < tbl + 1 + cnt * 2 or starts[i + 1] <= starts[i] or starts[i + 1] > w_end:
            raise ValueError(f"Non-monotonic widget offset at index {i}")
    widgets = [rec[starts[i] : starts[i + 1]] for i in range(cnt)]
    tail = rec[w_end:r_len]
    return bytearray(rec[:tbl]), tbl, widgets, tail


def patch_viewstlrec(uxc_bytes: bytes) -> bytes:
    """Build the 2380-byte modified RecordPtr payload for 0x02008029 from viewStlrec.uxc."""
    rec_lvl = _extract_uxc_record(uxc_bytes, SLOT_LEVEL_OUTER)
    rec_hist = _extract_uxc_record(uxc_bytes, SLOT_HIST_OUTER)

    _, _, hist_widgets, _ = _parse_record_ptr(rec_hist)
    src_master = [
        w
        for w in hist_widgets
        if len(w) == 60 and struct.unpack_from("<I", w, 4)[0] == WID_HIST_SRC_MASTER
    ]
    if len(src_master) != 1:
        raise ValueError(
            f"Expected unique {WID_HIST_SRC_MASTER:#010x} master widget in slot {SLOT_HIST_OUTER:#x}, "
            f"found {len(src_master)}"
        )

    cloned = bytearray(src_master[0])
    struct.pack_into("<I", cloned, 4, WID_NEW_MASTER)

    hdr, tbl, lvl_widgets, tail = _parse_record_ptr(rec_lvl)
    if any(
        len(w) >= 8 and struct.unpack_from("<I", w, 4)[0] == WID_NEW_MASTER
        for w in lvl_widgets
    ):
        raise ValueError(f"Target widget ID {WID_NEW_MASTER:#010x} already present in level layout")

    lvl_widgets.append(bytes(cloned))
    new_cnt = len(lvl_widgets)
    pos = 1 + new_cnt * 2
    new_offs: list[int] = []
    body = bytearray()
    for w in lvl_widgets:
        new_offs.append(pos)
        body.extend(w)
        pos += len(w)

    new_w_end = tbl + pos
    new_r_len = new_w_end + len(tail)
    struct.pack_into("<III", hdr, 0x1C, new_w_end, new_r_len, new_r_len)
    out = (
        bytes(hdr)
        + bytes([new_cnt])
        + struct.pack(f"<{new_cnt}H", *new_offs)
        + bytes(body)
        + tail
    )
    if len(out) != EXPECTED_PATCHED_LEN:
        raise ValueError(
            f"Unexpected patched payload size {len(out)} bytes (expected {EXPECTED_PATCHED_LEN}; "
            "verify viewStlrec.uxc is from Sony ILCE-7RM3 v3.10)"
        )
    return out


def format_local_header(payload: bytes) -> str:
    """Format the gitignored C header consumed by `src/a7r3_dual_hud.c`."""
    lines = [
        "#ifndef A7R3_DUAL_HUD_PAYLOAD_H",
        "#define A7R3_DUAL_HUD_PAYLOAD_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define A7R3_DUAL_HUD_PAYLOAD_LEN {len(payload)}u",
        "",
        "/* Locally generated from user's A7R3 v3.10 viewStlrec.uxc via tools/gen_dual_hud_payload.py.",
        " * tag = 0x00000000 (bit 31 clear -> inline RecordPtr data at +4). */",
        "static const struct {",
        "    uint32_t tag;",
        "    uint8_t payload[A7R3_DUAL_HUD_PAYLOAD_LEN];",
        "} __attribute__((aligned(4))) g_dual_level_rec = {",
        "    0x00000000u,",
        "    {",
    ]
    for i in range(0, len(payload), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in payload[i : i + 16])
        comma = "," if i + 16 < len(payload) else ""
        lines.append(f"        {chunk}{comma}")
    lines.extend(
        [
            "    }",
            "};",
            "",
            "#endif /* A7R3_DUAL_HUD_PAYLOAD_H */",
            "",
        ]
    )
    return "\n".join(lines)


def pull_uxc_from_camera(force: bool = False) -> bytes:
    """Read `/usr/share/app/viewStlrec.uxc` read-only over USB Service Terminal."""
    from deploy_ram import (
        SenserPlatformBackend,
        SonySenserCamera,
        ensure_service_terminal,
        open_usb_drivers,
        read_camera_file_verified,
        require_usb_runtime,
    )
    from ram_loader.terminal_shell import shell_command

    require_usb_runtime()
    with open_usb_drivers() as driver:
        dev = ensure_service_terminal(driver, force=force)
        backend = SenserPlatformBackend(SonySenserCamera(dev))
        sh = lambda cmd, timeout=15: shell_command(dev, cmd, timeout=timeout)
        return read_camera_file_verified(backend, sh, CAMERA_UXC_PATH)


def generate_local_header(
    uxc_bytes: bytes,
    out_path: Path = DEFAULT_OUT,
) -> Path:
    payload = patch_viewstlrec(uxc_bytes)
    text = format_local_header(payload)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text, encoding="utf-8")
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Generate gitignored src/a7r3_dual_hud_payload.local.h from viewStlrec.uxc"
    )
    ap.add_argument("--uxc", type=Path, default=None, help="Path to local viewStlrec.uxc")
    ap.add_argument(
        "--fw-lib-dir",
        type=Path,
        default=None,
        help="Path to A7R3 v3.10 /lib directory (auto-searches sibling /usr/share/app/viewStlrec.uxc)",
    )
    ap.add_argument(
        "--from-camera",
        action="store_true",
        help="Read /usr/share/app/viewStlrec.uxc read-only over USB from connected A7R3",
    )
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"Output header (default: {DEFAULT_OUT})")
    ap.add_argument("--force", action="store_true", help="Bypass camera model check when using --from-camera")
    args = ap.parse_args()

    try:
        if args.from_camera:
            print(f"[*] Reading {CAMERA_UXC_PATH} read-only from connected A7R3...")
            uxc_bytes = pull_uxc_from_camera(force=args.force)
            source_desc = f"camera:{CAMERA_UXC_PATH}"
        else:
            uxc_path = find_local_uxc(fw_lib_dir=args.fw_lib_dir, cli_uxc=args.uxc)
            if uxc_path is None:
                print(
                    "[!] Could not locate viewStlrec.uxc locally.\n"
                    "    Pass `--uxc /path/to/viewStlrec.uxc`, set `A7R3_UXC_PATH`,\n"
                    "    or run `python tools/gen_dual_hud_payload.py --from-camera` with camera connected.",
                    file=sys.stderr,
                )
                return 1
            uxc_bytes = uxc_path.read_bytes()
            source_desc = str(uxc_path)

        out = generate_local_header(uxc_bytes, out_path=args.out)
        print(f"[+] Generated {out} ({EXPECTED_PATCHED_LEN} bytes) from {source_desc}")
        return 0
    except Exception as exc:
        print(f"[!] Failed to generate dual-HUD payload: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
