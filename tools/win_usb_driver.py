#!/usr/bin/env python3
"""Windows temporary libusb-win32 driver manager for Sony A7R3 (MIT).

Temporarily binds `libusb-win32` (`libusb0`) to Sony Mass Storage (`054C:0BFF`)
and Service Mode (`054C:0336` / `054C:02A9`) using bundled `wdi-simple.exe`
(pbatard/libwdi, LGPL-3.0), then restores native Windows `USBSTOR` and removes
all temporary Sony `libusb-win32` INF packages via `pnputil` when done.

All device and driver store detection is locale-independent (JSON / INF text).
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent
WDI_SIMPLE_EXE = TOOLS_DIR / "win_driver" / "wdi-simple.exe"
BACKUP_FILE = SDK_DIR / "build" / "driver_backup.json"
WINDOWS_INF_DIR = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "INF"

SONY_VID = 0x054C
SENSER_PIDS = (0x0336, 0x02A9)
KNOWN_LIBUSB_PROVIDERS = ("libusb-win32", "libusb0", "libwdi")
LIBUSB_WIN32_CLASS_GUID = "{EB781AAF-9C70-4523-A5DF-642A87ECA567}"

_HIDDEN_SUBPROCESS_KW: dict = (
    {"creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0)}
    if sys.platform == "win32"
    else {}
)


def is_windows() -> bool:
    return sys.platform == "win32"


def is_admin() -> bool:
    if not is_windows():
        return True
    try:
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def require_admin(action_desc: str = "自动切换/还原 Windows USB 驱动") -> None:
    if not is_windows():
        return
    if not is_admin():
        raise PermissionError(
            f"{action_desc}需要 Windows 管理员权限。\n"
            "请双击根目录的 `一键注入拓展菜单.bat`（会自动请求 UAC 提权），"
            "或右键“以管理员身份运行”终端后重试。"
        )


def _decode_inf_text(raw: bytes) -> str:
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        return raw.decode("utf-16", errors="replace")
    if b"\x00" in raw[:256]:
        return raw.decode("utf-16-le", errors="replace")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("latin-1", errors="replace")


def list_sony_pnp_devices() -> list[dict]:
    """Query Windows PnP Sony USB devices as normalized dicts (locale-independent)."""
    if not is_windows():
        return []
    ps_cmd = (
        "Get-PnpDevice | Where-Object { $_.InstanceId -match '^USB\\\\VID_054C&PID_' } "
        "| Select-Object Status,Class,FriendlyName,InstanceId,Service "
        "| ConvertTo-Json -Compress"
    )
    res = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps_cmd],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=20,
        **_HIDDEN_SUBPROCESS_KW,
    )
    out = res.stdout.strip()
    if not out:
        return []
    try:
        parsed = json.loads(out)
    except json.JSONDecodeError:
        return []
    items = [parsed] if isinstance(parsed, dict) else list(parsed)
    result: list[dict] = []
    for item in items:
        inst = str(item.get("InstanceId") or "")
        m = re.search(r"VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})", inst)
        if not m:
            continue
        status = str(item.get("Status") or "")
        service = str(item.get("Service") or "")
        cls = str(item.get("Class") or "")
        result.append(
            {
                "vid": int(m.group(1), 16),
                "pid": int(m.group(2), 16),
                "instance_id": inst,
                "status": status,
                "present": status.upper() == "OK",
                "class_name": cls,
                "friendly_name": str(item.get("FriendlyName") or ""),
                "service": service,
                "service_lower": service.lower(),
            }
        )
    return result


def find_sony_libusb_infs(pids: set[int] | None = None) -> list[dict]:
    """Scan C:\\Windows\\INF\\oem*.inf directly (locale-independent) for Sony libusb-win32 INFs."""
    if not is_windows() or not WINDOWS_INF_DIR.is_dir():
        return []
    found: list[dict] = []
    for inf_path in sorted(WINDOWS_INF_DIR.glob("oem*.inf")):
        try:
            text = _decode_inf_text(inf_path.read_bytes())
        except OSError:
            continue
        low = text.lower()
        if "vid_054c&pid_" not in low:
            continue
        is_libusb = (
            LIBUSB_WIN32_CLASS_GUID.lower() in low
            or "libusb-win32" in low
            or "libusb0.sys" in low
            or "libwdi" in low
        )
        if not is_libusb:
            continue
        inf_pids = {int(x, 16) for x in re.findall(r"vid_054c&pid_([0-9a-f]{4})", low)}
        if pids is not None and not (inf_pids & pids):
            continue
        name_m = re.search(r'(?im)^\s*DeviceName\s*=\s*"([^"]+)"', text)
        prov_m = re.search(r'(?im)^\s*Provider\s*=\s*"([^"]+)"', text)
        found.append(
            {
                "published_name": inf_path.name,
                "path": str(inf_path),
                "pids": sorted(inf_pids),
                "device_name": name_m.group(1) if name_m else "",
                "provider": prov_m.group(1) if prov_m else "",
            }
        )
    return found


def snapshot_driver_state() -> dict:
    return {
        "timestamp": int(time.time()),
        "devices": list_sony_pnp_devices(),
        "libusb_infs": find_sony_libusb_infs(),
    }


def save_backup(state: dict | None = None) -> Path:
    BACKUP_FILE.parent.mkdir(parents=True, exist_ok=True)
    data = state if state is not None else snapshot_driver_state()
    BACKUP_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    return BACKUP_FILE


def clear_backup() -> None:
    try:
        if BACKUP_FILE.is_file():
            BACKUP_FILE.unlink()
    except OSError:
        pass


def install_libusb0_for_pid(
    vid: int,
    pid: int,
    device_name: str,
    timeout: int = 120,
) -> None:
    """Install temporary libusb-win32 driver for a connected USB device via bundled wdi-simple.exe."""
    require_admin("安装临时 libusb-win32 驱动")
    if not WDI_SIMPLE_EXE.is_file():
        raise FileNotFoundError(f"缺少 Windows 驱动工具: {WDI_SIMPLE_EXE}")

    inf_name = f"a7r3_temp_{vid:04x}_{pid:04x}.inf"
    print(f"[*] 正在为 USB 设备 0x{vid:04x}:0x{pid:04x} 安装临时 libusb-win32 驱动...")
    with tempfile.TemporaryDirectory(prefix="a7r3_wdi_") as tmp_dir:
        cmd = [
            str(WDI_SIMPLE_EXE),
            "--vid", f"0x{vid:04x}",
            "--pid", f"0x{pid:04x}",
            "--type", "1",  # WDI_LIBUSB0 (libusb-win32)
            "--name", device_name,
            "--inf", inf_name,
            "--dest", tmp_dir,
            "--timeout", "60000",
            "--silent",
        ]
        res = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            **_HIDDEN_SUBPROCESS_KW,
        )

    deadline = time.time() + 15.0
    bound = False
    while time.time() < deadline:
        devs = list_sony_pnp_devices()
        if any(d["present"] and d["vid"] == vid and d["pid"] == pid and d["service_lower"] == "libusb0" for d in devs):
            bound = True
            break
        time.sleep(0.5)

    if res.returncode != 0 and not bound:
        detail = (res.stdout + "\n" + res.stderr).strip()
        raise RuntimeError(
            f"wdi-simple.exe 安装 0x{vid:04x}:0x{pid:04x} 驱动失败 (exit={res.returncode})"
            + (f":\n{detail}" if detail else "")
        )
    if not bound:
        raise RuntimeError(f"驱动安装返回完成，但未检测到 0x{vid:04x}:0x{pid:04x} 绑定到 libusb0。")
    print(f"[+] 0x{vid:04x}:0x{pid:04x} 已绑定临时 libusb-win32 驱动。")


def ensure_camera_driver_for_entry() -> dict:
    """Ensure the currently connected Sony A7R3 (MSC or Service Mode) uses libusb0."""
    require_admin("自动切换相机 USB 驱动")
    present = [d for d in list_sony_pnp_devices() if d["present"]]
    if not present:
        raise RuntimeError(
            "未检测到已连接的 Sony 相机。\n"
            "请确认相机已开机、使用数据线直连电脑，"
            "且相机 MENU -> 设置 -> USB连接 设为「大容量存储 (Mass Storage)」。"
        )

    for d in present:
        if d["pid"] in SENSER_PIDS:
            if d["service_lower"] != "libusb0":
                install_libusb0_for_pid(SONY_VID, d["pid"], "Sony Service Mode (Temp libusb)")
            else:
                print(f"[*] 服务模式设备 0x054C:0x{d['pid']:04X} 已使用 libusb-win32。")
            return {"mode": "senser", "pid": d["pid"]}

    msc_devs = [
        d for d in present
        if d["service_lower"] in ("usbstor", "libusb0")
        or d["class_name"].lower() in ("usb", "libusb-win32 devices")
    ]
    if msc_devs:
        d = msc_devs[0]
        if d["service_lower"] != "libusb0":
            install_libusb0_for_pid(SONY_VID, d["pid"], f"Sony {d['friendly_name'] or 'MSC'} (Temp libusb)")
        else:
            print(f"[*] 大容量存储设备 0x054C:0x{d['pid']:04X} 已使用 libusb-win32。")
        return {"mode": "msc", "pid": d["pid"]}

    if any("mtp" in d["service_lower"] or d["class_name"].upper() == "WPD" for d in present):
        raise RuntimeError(
            "检测到相机当前处于 MTP 模式。\n"
            "请在相机 MENU -> 设置(黄箱子) -> USB连接 改为「大容量存储 (Mass Storage)」后重试。"
        )

    desc = ", ".join(f"0x{d['pid']:04X}({d['service'] or d['class_name']})" for d in present)
    raise RuntimeError(f"检测到未知状态的 Sony USB 设备: {desc}")


def ensure_service_mode_driver_ready(seen_pids: set[int] | None = None) -> bool:
    """Called during MSC -> Service Mode transition: auto-bind libusb0 if 0336/02A9 appears unbound."""
    if not is_windows():
        return False
    present = [d for d in list_sony_pnp_devices() if d["present"] and d["pid"] in SENSER_PIDS]
    for d in present:
        if d["service_lower"] == "libusb0":
            return True
        if seen_pids is not None and d["pid"] in seen_pids:
            continue
        if not is_admin():
            return False
        if seen_pids is not None:
            seen_pids.add(d["pid"])
        install_libusb0_for_pid(SONY_VID, d["pid"], "Sony Service Mode (Temp libusb)")
        return True
    return False


def restore_all_sony_drivers(verbose: bool = True) -> dict:
    """Remove ALL Sony (VID 054C) libusb-win32 INF packages and re-scan PnP to restore USBSTOR."""
    if not is_windows():
        return {"removed": [], "remaining": [], "errors": []}
    require_admin("还原相机原生 USB 驱动")

    infs = find_sony_libusb_infs()
    removed: list[dict] = []
    errors: list[str] = []

    if infs and verbose:
        print(f"[*] 正在卸载 {len(infs)} 个 Sony 临时 libusb-win32 驱动包并恢复原生驱动...")

    for info in infs:
        pub = info["published_name"]
        pids_str = ",".join(f"0x{p:04X}" for p in info["pids"])
        res = subprocess.run(
            ["pnputil", "/delete-driver", pub, "/uninstall", "/force"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
            **_HIDDEN_SUBPROCESS_KW,
        )
        if res.returncode in (0, 3010):
            removed.append(info)
            if verbose:
                print(f"[+] 已卸载临时驱动 {pub} (PID={pids_str})")
        else:
            msg = f"{pub} (PID={pids_str}) 卸载失败 exit={res.returncode}: {(res.stdout + res.stderr).strip()}"
            errors.append(msg)
            if verbose:
                print(f"[!] {msg}", file=sys.stderr)

    # Trigger PnP rescan so connected MSC devices immediately re-bind to USBSTOR.
    subprocess.run(
        ["pnputil", "/scan-devices"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=60,
        **_HIDDEN_SUBPROCESS_KW,
    )
    time.sleep(1.0)

    remaining = find_sony_libusb_infs()
    if not remaining and not errors:
        clear_backup()
        if verbose:
            present = [d for d in list_sony_pnp_devices() if d["present"]]
            usbstor_devs = [d for d in present if d["service_lower"] == "usbstor"]
            if usbstor_devs:
                print("[+] 相机已恢复为 Windows 原生 USBSTOR 驱动。")
            else:
                print("[+] 全部 Sony 临时 libusb-win32 驱动已清除；下次以大容量存储模式连接将直接使用原生 USBSTOR。")
    return {"removed": removed, "remaining": remaining, "errors": errors}


class SonyDriverGuard:
    """Context manager that temporarily binds libusb-win32 and restores all Sony drivers on exit."""

    def __init__(self, auto_restore: bool = True):
        self.auto_restore = auto_restore
        self.active = False

    def __enter__(self):
        if not is_windows():
            return self
        require_admin("一键自动驱动管理")
        state = snapshot_driver_state()
        save_backup(state)
        self.active = True
        ensure_camera_driver_for_entry()
        return self

    def __exit__(self, exc_type, exc, tb):
        if not self.active or not self.auto_restore or not is_windows():
            return False
        try:
            res = restore_all_sony_drivers(verbose=True)
            if res["errors"]:
                print(
                    "[!] 提示：部分临时驱动未完全移除，请稍后运行 `python tools/a7r3_addon_console.py restore-driver`。",
                    file=sys.stderr,
                )
        except Exception as restore_exc:
            print(f"[!] 自动还原驱动时出错: {restore_exc}", file=sys.stderr)
        return False


def format_status_lines() -> list[str]:
    if not is_windows():
        return ["非 Windows 系统，无需 libusb-win32 驱动替换/还原。"]
    lines: list[str] = []
    devs = list_sony_pnp_devices()
    present = [d for d in devs if d["present"]]
    infs = find_sony_libusb_infs()
    if present:
        for d in present:
            lines.append(
                f"当前连接: 0x054C:0x{d['pid']:04X} | Class={d['class_name']} | Service={d['service'] or '(none)'} | {d['friendly_name']}"
            )
    else:
        lines.append("当前连接: 未检测到在线的 Sony (0x054C) USB 设备")
    if infs:
        for inf in infs:
            pids_str = ",".join(f"0x{p:04X}" for p in inf["pids"])
            lines.append(f"已安装 libusb-win32 驱动包: {inf['published_name']} (PID={pids_str}, {inf['device_name'] or inf['provider']})")
    else:
        lines.append("已安装 libusb-win32 驱动包: 无 (系统处于干净原生驱动状态)")
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description="Sony A7R3 Windows USB driver manager")
    ap.add_argument("action", choices=("status", "install-msc", "install-senser", "restore"))
    args = ap.parse_args()

    if args.action == "status":
        for line in format_status_lines():
            print(line)
        return 0
    if args.action == "install-msc":
        info = ensure_camera_driver_for_entry()
        print(f"OK: {info}")
        return 0
    if args.action == "install-senser":
        present = [d for d in list_sony_pnp_devices() if d["present"] and d["pid"] in SENSER_PIDS]
        pid = present[0]["pid"] if present else SENSER_PIDS[0]
        install_libusb0_for_pid(SONY_VID, pid, "Sony Service Mode (Temp libusb)")
        return 0
    if args.action == "restore":
        res = restore_all_sony_drivers(verbose=True)
        return 0 if not res["errors"] and not res["remaining"] else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
