#!/usr/bin/env python3
"""Build a freestanding ARMv7-A pure-RAM A7R3 overlay add-on shared library."""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent
SRC_DIR = SDK_DIR / "src"
BUILD_DIR = SDK_DIR / "build"

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
from env_config import load_local_env  # noqa: E402

load_local_env(SDK_DIR)

CORE_SRCS = [
    SRC_DIR / "a7r3_sys.c",
    SRC_DIR / "a7r3_fb.c",
    SRC_DIR / "a7r3_draw.c",
    SRC_DIR / "a7r3_md3.c",
    SRC_DIR / "a7r3_input.c",
    SRC_DIR / "a7r3_dual_hud.c",
    SRC_DIR / "a7r3_pb_focus.c",
    SRC_DIR / "a7r3_entry_cert.c",
    SRC_DIR / "a7r3_shutter_sfx.c",
    SRC_DIR / "a7r3_menu_touch.c",
    SRC_DIR / "a7r3_native_tab7.c",
]


def resolve_llvm_bin(cli_val: Path | None) -> Path | None:
    if cli_val is not None:
        return cli_val
    if env_val := os.environ.get("A7R3_LLVM_BIN", "").strip():
        return Path(env_val)
    if lld_in_path := shutil.which("ld.lld"):
        cand = Path(lld_in_path).resolve().parent
        if (cand / "clang").exists() or (cand / "clang.exe").exists():
            return cand
    for brew_dir in (Path("/opt/homebrew/opt/llvm/bin"), Path("/usr/local/opt/llvm/bin")):
        if (brew_dir / "clang").is_file() and (brew_dir / "ld.lld").is_file():
            return brew_dir
    if clang_in_path := shutil.which("clang"):
        return Path(clang_in_path).resolve().parent
    return None


def resolve_fw_lib_dir(cli_val: Path | None) -> Path | None:
    if cli_val is not None:
        return cli_val
    if env_val := os.environ.get("A7R3_FW_LIB_DIR", "").strip():
        return Path(env_val)
    for local_cand in (SDK_DIR / "firmware" / "lib", SDK_DIR / "extraction" / "files" / "lib"):
        if (
            (local_cand / "libc-2.11.2.so").is_file()
            and (local_cand / "libdl-2.11.2.so").is_file()
            and (local_cand / "libpthread-2.11.2.so").is_file()
        ):
            return local_cand
    return None


def tool_exe(llvm_bin: Path, base_name: str) -> Path:
    exe = llvm_bin / f"{base_name}.exe"
    return exe if exe.exists() else llvm_bin / base_name


def exported_symbols(readelf: Path, so_path: Path) -> set[str]:
    out = subprocess.check_output(
        [str(readelf), "--dyn-syms", "-W", str(so_path)],
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    syms: set[str] = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[6] != "UND" and parts[0].rstrip(":").isdigit():
            syms.add(parts[-1].split("@", 1)[0])
    return syms


def ensure_generated_labels(src_path: Path) -> int:
    labels_h = SRC_DIR / "md3_demo_labels.h"
    if labels_h.exists():
        return 0
    try:
        src_text = src_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0
    if "md3_demo_labels.h" not in src_text:
        return 0
    gen_script = TOOLS_DIR / "gen_md3_assets.py"
    if not gen_script.is_file():
        print(f"[!] Missing {labels_h} and generator {gen_script}", file=sys.stderr)
        return 1
    print("[*] md3_demo_labels.h not found locally; generating from system CJK fonts...")
    res = subprocess.run([sys.executable, str(gen_script)], text=True)
    return res.returncode


def ensure_dual_hud_payload(fw_lib_dir: Path | None) -> None:
    """Auto-generate gitignored src/a7r3_dual_hud_payload.local.h if viewStlrec.uxc is nearby."""
    payload_h = SRC_DIR / "a7r3_dual_hud_payload.local.h"
    if payload_h.is_file():
        return
    try:
        from gen_dual_hud_payload import find_local_uxc, generate_local_header
    except Exception:
        return
    uxc_path = find_local_uxc(fw_lib_dir=fw_lib_dir)
    if uxc_path is not None:
        try:
            generate_local_header(uxc_path.read_bytes(), out_path=payload_h)
            print(f"[+] Auto-generated local {payload_h.name} from {uxc_path}")
            return
        except Exception as exc:
            print(f"[!] Warning: failed to generate {payload_h.name} from {uxc_path}: {exc}")
    print(
        "[*] Info: src/a7r3_dual_hud_payload.local.h not present and viewStlrec.uxc not found locally; "
        "Dual-HUD toggle will be grayed out (pass-through). Run `python tools/gen_dual_hud_payload.py "
        "--from-camera` (or `--uxc <viewStlrec.uxc>`) to enable."
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--src",
        type=Path,
        default=SRC_DIR / "demo_addon_menu.c",
        help="Add-on entry C file (default: src/demo_addon_menu.c)",
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=BUILD_DIR / "demo_addon_ram.so",
        help="Output .so path (default: build/demo_addon_ram.so)",
    )
    ap.add_argument(
        "--llvm-bin",
        type=Path,
        default=None,
        help="Path to LLVM bin directory (or set A7R3_LLVM_BIN / .env.local)",
    )
    ap.add_argument(
        "--fw-lib-dir",
        type=Path,
        default=None,
        help="Path to locally extracted A7R3 v3.10 /lib directory (or set A7R3_FW_LIB_DIR / .env.local)",
    )
    args = ap.parse_args()

    llvm_bin = resolve_llvm_bin(args.llvm_bin)
    fw_lib_dir = resolve_fw_lib_dir(args.fw_lib_dir)

    if llvm_bin is None:
        print(
            "[!] LLVM toolchain not found. Set A7R3_LLVM_BIN in .env.local or pass --llvm-bin.",
            file=sys.stderr,
        )
        return 1
    if fw_lib_dir is None:
        print(
            "[!] Firmware /lib directory not configured.\n"
            "    Run `python tools/a7r3_addon_console.py init-camera` with camera connected,\n"
            "    or set A7R3_FW_LIB_DIR in .env.local (or pass --fw-lib-dir).",
            file=sys.stderr,
        )
        return 1

    clang = tool_exe(llvm_bin, "clang")
    lld = tool_exe(llvm_bin, "ld.lld")
    readelf = tool_exe(llvm_bin, "llvm-readelf")

    libc_so = fw_lib_dir / "libc-2.11.2.so"
    libdl_so = fw_lib_dir / "libdl-2.11.2.so"
    libpthread_so = fw_lib_dir / "libpthread-2.11.2.so"

    for p in (clang, lld, readelf, libc_so, libdl_so, libpthread_so, args.src, *CORE_SRCS):
        if not p.exists():
            print(f"[!] Missing required file: {p}", file=sys.stderr)
            print(
                "[!] Copy .env.example to .env.local (or export A7R3_LLVM_BIN / A7R3_FW_LIB_DIR). "
                "See docs/TOOLS_AND_BUILD.md.",
                file=sys.stderr,
            )
            return 1

    rc_gen = ensure_generated_labels(args.src)
    if rc_gen != 0:
        return rc_gen
    ensure_dual_hud_payload(fw_lib_dir)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(clang),
        "--target=arm-linux-gnueabi",
        "-march=armv7-a",
        "-mfloat-abi=soft",
        "-shared",
        "-fPIC",
        "-O2",
        f"-fuse-ld={lld}",
        "-nostdlib",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-fvisibility=hidden",
        f"-I{SRC_DIR}",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-unused-parameter",
        "-Wl,-z,now",
        "-Wl,-Bsymbolic",
        "-Wl,-z,nocombreloc",
        f"-L{fw_lib_dir}",
        "-Wl,--no-as-needed",
        "-l:libc-2.11.2.so",
        "-l:libdl-2.11.2.so",
        "-l:libpthread-2.11.2.so",
        "-o",
        str(args.out),
        *[str(s) for s in CORE_SRCS],
        str(args.src),
    ]
    print("[*] Compiling:", " ".join(cmd))
    res = subprocess.run(cmd, text=True)
    if res.returncode != 0:
        return res.returncode

    print(f"[+] Built {args.out} ({args.out.stat().st_size} bytes)")

    dyn_out = subprocess.check_output([str(readelf), "-d", str(args.out)], text=True)
    needed = [l.strip() for l in dyn_out.splitlines() if "NEEDED" in l]
    print("[*] DT_NEEDED:")
    for n in needed:
        print("   ", n)
    if not any("libc.so.6" in n for n in needed) or not any("libpthread.so.0" in n for n in needed):
        print("[!] ERROR: missing libc.so.6 or libpthread.so.0 in DT_NEEDED", file=sys.stderr)
        return 2
    if "(RELCOUNT)" in dyn_out:
        print("[!] ERROR: DT_RELCOUNT emitted (breaks glibc 2.11.2 ld.so BIND_NOW)", file=sys.stderr)
        return 2

    sym_out = subprocess.check_output([str(readelf), "--dyn-syms", "-W", str(args.out)], text=True)
    und: list[tuple[str, str]] = []
    for line in sym_out.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[6] == "UND" and parts[0].rstrip(":").isdigit():
            name = parts[-1].split("@", 1)[0]
            if name:
                und.append((parts[4], name))

    allowed = (
        exported_symbols(readelf, libc_so)
        | exported_symbols(readelf, libdl_so)
        | exported_symbols(readelf, libpthread_so)
    )
    print(f"[*] Undefined symbols ({len(und)}):")
    bad = []
    for bind, name in und:
        ok = name in allowed
        mark = "OK" if ok else "MISSING"
        print(f"    {bind:<6} {name:<24} [{mark}]")
        if not ok:
            bad.append((bind, name))

    if bad:
        print(f"[!] ERROR: unresolved symbols: {bad}", file=sys.stderr)
        return 3

    print("[+] ELF verification passed: zero unresolved external symbols!")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
