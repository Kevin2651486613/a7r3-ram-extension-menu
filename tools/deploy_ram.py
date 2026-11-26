#!/usr/bin/env python3
"""Pure-RAM deployer for A7R3 extension menu shared libraries (/tmp_bt only, zero flash writes)."""
import argparse
import hashlib
import io
import os
import re
import struct
import sys
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
from env_config import load_local_env  # noqa: E402

load_local_env(SDK_DIR)

HUD_DIR_ENV = os.environ.get("A7R3_HUD_DIR", "").strip()
PMCA_DIR_ENV = os.environ.get("A7R3_PMCA_DIR", "").strip()
HUD_DIR = Path(HUD_DIR_ENV) if HUD_DIR_ENV else None

for _extra in reversed(
    (
        TOOLS_DIR,
        Path(PMCA_DIR_ENV) if PMCA_DIR_ENV else None,
        TOOLS_DIR / "vendor",
        SDK_DIR / "vendor" / "Sony-PMCA-RE",
        SDK_DIR / "Sony-PMCA-RE",
        SDK_DIR.parent / "Sony-PMCA-RE",
        (HUD_DIR / "vendor").resolve() if HUD_DIR else None,
    )
):
    if _extra is not None and _extra.exists() and str(_extra) not in sys.path:
        sys.path.insert(0, str(_extra))

_HELPER_IMPORT_ERR: Exception | None = None
try:
    from ram_loader.build_mem_writer import build_mem_writer_elf
    from ram_loader.build_show_hex import build_show_hex_elf
    from ram_loader.terminal_shell import shell_command
except Exception as exc:  # pragma: no cover
    _HELPER_IMPORT_ERR = exc
    build_mem_writer_elf = None  # type: ignore[assignment]
    build_show_hex_elf = None  # type: ignore[assignment]
    shell_command = None  # type: ignore[assignment]

_USB_IMPORT_ERR: Exception | None = None
try:
    from pmca.commands.usb import UsbDriverList, getDevice, importDriver, listDevices
    from pmca.platform.backend.senser import SenserPlatformBackend
    from pmca.usb.driver.generic import libusb as _pmca_libusb
    from pmca.usb.sony import (
        SONY_ID_PRODUCT_SENSER,
        SONY_ID_VENDOR,
        SonyMscExtCmdDevice,
        SonyMtpExtCmdDevice,
        SonySenserAuthDevice,
        SonySenserCamera,
        SonySenserDevice,
    )

    # Never drop USB controller on exit.
    SonySenserAuthDevice.stop = lambda self: None
except Exception as exc:  # pragma: no cover - deferred until USB actions run
    _USB_IMPORT_ERR = exc
    UsbDriverList = None  # type: ignore[assignment]
    getDevice = None  # type: ignore[assignment]
    importDriver = None  # type: ignore[assignment]
    listDevices = None  # type: ignore[assignment]
    _pmca_libusb = None  # type: ignore[assignment]
    SONY_ID_PRODUCT_SENSER = (0x02A9, 0x0336)
    SONY_ID_VENDOR = 0x054C
    SenserPlatformBackend = None  # type: ignore[assignment]
    SonyMscExtCmdDevice = None  # type: ignore[assignment]
    SonyMtpExtCmdDevice = None  # type: ignore[assignment]
    SonySenserAuthDevice = None  # type: ignore[assignment]
    SonySenserCamera = None  # type: ignore[assignment]
    SonySenserDevice = None  # type: ignore[assignment]

LIBC_SVC_OFF = 0x00024F78
LIBDL_DLOPEN_OFF = 0x00000BC9
LIBDL_DLERROR_OFF = 0x00000E05

MAX_SO_BYTES = 4 * 1024 * 1024
MIN_SO_BYTES = 4096
MAX_TMPBT_UPLOAD_BYTES = 8 * 1024 * 1024
TMPBT_PATH_RE = re.compile(r"^/tmp_bt/[A-Za-z0-9._-]+$")
REQUIRED_IM_LIBS = (
    "/libc-2.11.2.so",
    "/libdl-2.11.2.so",
    "/libObj.so",
    "/viewUnified2.so",
    "/libBizFw.so",
    "/libInfraFsys.so",
)


def require_usb_runtime() -> None:
    if _HELPER_IMPORT_ERR is not None:
        raise RuntimeError(
            f"内置纯 RAM 注入助手加载失败 ({_HELPER_IMPORT_ERR})。"
            "请确认已安装依赖: `pip install -r requirements.txt`。"
        )
    if _USB_IMPORT_ERR is not None:
        raise RuntimeError(
            f"USB 运行时依赖加载失败 ({_USB_IMPORT_ERR})。"
            "请先安装 `pip install -r requirements.txt`（已内置精简版 tools/vendor/pmca，无需单独克隆 Sony-PMCA-RE）。"
        )


def validate_tmpbt_path(remote_path: str) -> None:
    if not TMPBT_PATH_RE.fullmatch(remote_path):
        raise ValueError(
            f"拒绝不安全的远程路径: {remote_path!r} (仅允许 /tmp_bt/<文件名>，禁止路径穿越或特殊字符)"
        )
    if ".." in remote_path:
        raise ValueError(f"拒绝包含 '..' 的远程路径: {remote_path!r}")


def validate_tmpbt_data(data: bytes) -> None:
    if not data:
        raise ValueError("拒绝向 /tmp_bt 上传空数据 (0 bytes)")
    if len(data) > MAX_TMPBT_UPLOAD_BYTES:
        raise ValueError(
            f"上传文件过大 ({len(data)} bytes > {MAX_TMPBT_UPLOAD_BYTES} bytes)，拒绝写入 /tmp_bt"
        )


def validate_arm32_shared_elf(so_path: Path) -> bytes:
    if not so_path.exists():
        raise FileNotFoundError(f"找不到插件文件: {so_path}")
    if not so_path.is_file():
        raise ValueError(f"目标路径不是普通文件: {so_path}")

    size = so_path.stat().st_size
    if size < MIN_SO_BYTES or size > MAX_SO_BYTES:
        raise ValueError(
            f"插件大小越界 ({size} bytes)，允许范围为 {MIN_SO_BYTES} ~ {MAX_SO_BYTES} bytes"
        )

    data = so_path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise ValueError(f"文件头不是有效的 ELF 格式: {so_path}")

    ei_class = data[4]
    ei_data = data[5]
    if ei_class != 1 or ei_data != 1:
        raise ValueError(
            f"非 ELF32 小端格式 (class={ei_class}, data={ei_data})，目标设备要求 ARM32 LE"
        )

    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_type != 3:
        raise ValueError(f"ELF 类型不是共享库 ET_DYN (e_type={e_type})")
    if e_machine != 40:
        raise ValueError(f"ELF 架构不是 ARM (e_machine={e_machine}, 期望 40)")
    return data


def check_camera_model(dev, force: bool = False) -> None:
    require_usb_runtime()
    cam = SonySenserCamera(dev)
    get_model = getattr(cam, "getModel", None)
    if not callable(get_model):
        return
    # Fail closed: if the model cannot be read, refuse to continue unless --force is given.
    try:
        model_info = get_model()
    except Exception as exc:
        if force:
            print(f"[!] 无法读取机身型号 ({exc})，已按 --force 跳过校验")
            return
        raise RuntimeError(
            f"无法读取机身型号，无法确认机型 ({exc})。"
            "当前逆向偏移仅针对 ILCE-7RM3 v3.10；如确需强制实验，请传 --force。"
        ) from exc
    model_str = str(model_info or "").strip()
    if not model_str:
        if force:
            print("[!] 机身型号为空，已按 --force 跳过校验")
            return
        raise RuntimeError(
            "机身型号为空，无法确认机型。"
            "当前逆向偏移仅针对 ILCE-7RM3 v3.10；如确需强制实验，请传 --force。"
        )
    print(f"[*] 检测到机身型号信息: {model_str}")
    if "ILCE-7RM3" not in model_str.upper() and not force:
        raise RuntimeError(
            f"机身型号校验未通过: {model_str!r} (当前逆向偏移仅针对 ILCE-7RM3 v3.10)。"
            "如确需强制实验，请传 --force。"
        )


def open_usb_drivers():
    """Open PMCA USB drivers safely across Windows, macOS, and Linux."""
    require_usb_runtime()
    if sys.platform == "win32":
        contexts = []
        try:
            from pmca.usb.driver.windows.msc import MscContext as WinMscContext

            contexts.append(WinMscContext())
        except Exception:
            pass
        contexts.extend(
            [
                _pmca_libusb.MscContext(),
                _pmca_libusb.MtpContext(),
                _pmca_libusb.VendorSpecificContext(),
            ]
        )
        print("Using drivers " + ", ".join(c.name for c in contexts))
        return UsbDriverList(*contexts)
    return importDriver(None)


def diagnose_sony_usb_state() -> str:
    """Inspect raw USB devices (VID 0x054c) to pinpoint driver/mode issues."""
    try:
        import usb.core

        devs = list(usb.core.find(find_all=True, idVendor=SONY_ID_VENDOR) or ())
    except Exception as exc:
        return (
            f"无法扫描 libusb 设备 ({exc})。"
            + (
                "Windows 请用 Zadig 安装 libusb-win32/libusbK。"
                if sys.platform == "win32"
                else ("macOS 请确认已执行 `brew install libusb`。" if sys.platform == "darwin" else "请安装 libusb-1.0-0 并检查权限。")
            )
        )

    if not devs:
        return (
            "USB 总线上未发现 Sony (0x054c) 设备：请确认相机已开机、使用带数据传输能力的 USB 线直连电脑，"
            "且相机 MENU -> 设置 -> USB连接 设为「大容量存储 (Mass Storage)」。"
        )

    lines = []
    saw_senser = False
    saw_mtp = False
    saw_msc = False
    for d in devs:
        pid = int(getattr(d, "idProduct", 0))
        classes = []
        try:
            for cfg in d:
                for intf in cfg:
                    classes.append(intf.bInterfaceClass)
        except Exception:
            pass
        if pid in SONY_ID_PRODUCT_SENSER or 0xFF in classes:
            saw_senser = True
        if 0x06 in classes:
            saw_mtp = True
        if 0x08 in classes:
            saw_msc = True
        cls_str = ",".join(f"0x{c:02x}" for c in sorted(set(classes))) or "unknown"
        lines.append(f"0x{SONY_ID_VENDOR:04x}:0x{pid:04x} (class={cls_str})")

    hints = []
    if saw_senser:
        if sys.platform == "win32":
            hints.append(
                "相机已处于 Service Mode，但 Windows 未能在该 PID 上打开 libusb！"
                "请保持相机连接，打开 Zadig -> Options -> List All Devices -> "
                "找到新出现的 Sony 设备（PID 0336/02A9），安装 `libusb-win32` 驱动后重试。"
            )
        elif sys.platform == "darwin":
            hints.append(
                "相机已处于 Service Mode，但 macOS 无法打开该接口（请确认 `brew install libusb` 且无权限/驱动占用问题）。"
            )
        else:
            hints.append(
                "相机已处于 Service Mode，但 Linux 无权限或被占用（请配置 054c udev 规则或用 sudo 重试）。"
            )
    elif saw_mtp:
        hints.append(
            "相机当前处于 MTP / PTP 模式（class=0x06）：请在相机 MENU -> 设置 -> USB连接 中改为「大容量存储 (Mass Storage)」。"
        )
    elif saw_msc:
        if sys.platform == "win32":
            hints.append(
                "相机处于 MSC 模式但切换失败：请尝试用 Zadig 为 Sony Mass Storage 绑定 `libusb-win32`，并确认相机未处于回放/菜单忙碌状态或更换 USB 直连口。"
            )
        elif sys.platform == "darwin":
            hints.append(
                "相机处于 MSC 模式但 macOS 系统驱动正占用磁盘（macOS 下 libusb 通常无法直接抢占已挂载的 MSC）："
                "请尝试先弹出/卸载相机磁盘卷 (`diskutil unmountDisk ...`)、安装 Sony 官方 PMCA DriverLoader kext，或避免使用转接 Hub。"
            )
        else:
            hints.append(
                "相机处于 MSC 模式但切换失败：请确认未被系统挂载占用（可先 umount），并检查 udev 规则或使用 sudo。"
            )
    return f"USB 总线检测到: {', '.join(lines)}。" + (" ".join(hints) if hints else "")


def ensure_service_terminal(driver, force: bool = False, on_wait_senser=None):
    require_usb_runtime()
    try:
        dev = getDevice(driver)
    except Exception as exc:
        raise RuntimeError(f"枚举 Sony 设备失败 ({exc})\nUSB 诊断: {diagnose_sony_usb_state()}") from exc
    if not dev:
        raise RuntimeError(f"No Sony device found\nUSB 诊断: {diagnose_sony_usb_state()}")

    if isinstance(dev, SonyMtpExtCmdDevice):
        raise RuntimeError(
            "检测到相机当前处于 MTP 模式，无法直接进入 Service Mode。\n"
            "请在相机 MENU -> 设置(黄箱子) -> USB连接 设为「大容量存储 (Mass Storage)」后重试。"
        )

    if isinstance(dev, SonySenserDevice):
        # Only the terminal liveness probe is allowed to trigger re-authentication.
        # The model check runs afterwards so a wrong-model error is not swallowed.
        try:
            for _ in range(5):
                dev.readTerminal()
                time.sleep(0.05)
            shell_command(dev, "echo PING", timeout=2)
            terminal_ready = True
        except Exception:
            terminal_ready = False

        if terminal_ready:
            print("[+] Service terminal already active.")
        else:
            auth = SonySenserAuthDevice(dev.driver)
            auth.start()
            auth.authenticate()
            SonySenserCamera(dev).setTerminalEnable(True)
            time.sleep(0.5)
            for _ in range(5):
                dev.readTerminal()
                time.sleep(0.05)
        check_camera_model(dev, force=force)
        return dev

    if isinstance(dev, SonyMscExtCmdDevice):
        if not hasattr(dev.driver, "vendorRequestOut"):
            raise RuntimeError(
                "当前相机 Mass Storage 接口仍使用 Windows 原生存储驱动，无法直接发送 Service Mode 控制指令。\n"
                "请改用根目录的 `一键注入拓展菜单.bat`（或执行 `python tools/a7r3_addon_console.py one-click`）自动完成临时驱动切换与还原。"
            )
        try:
            auth = SonySenserAuthDevice(dev.driver)
            auth.start()
            auth.authenticate()
        except Exception as exc:
            raise RuntimeError(
                f"向相机发送 Service Mode 认证握手失败 ({exc})\nUSB 诊断: {diagnose_sony_usb_state()}"
            ) from exc

        senser_dev = None
        deadline = time.monotonic() + 25.0
        attempt = 0
        while time.monotonic() < deadline:
            time.sleep(0.5)
            attempt += 1
            if callable(on_wait_senser) and attempt >= 3:
                try:
                    if on_wait_senser():
                        deadline = max(deadline, time.monotonic() + 10.0)
                except Exception as cb_exc:
                    raise RuntimeError(f"自动切换 Service Mode 驱动失败: {cb_exc}") from cb_exc
            try:
                devices = [d for d in listDevices(driver, True) if isinstance(d, SonySenserDevice)]
                if len(devices) == 1:
                    senser_dev = devices[0]
                    break
            except Exception:
                pass
        if not senser_dev:
            raise RuntimeError(
                "Timed out waiting for Service Mode (25s)\n"
                f"USB 诊断: {diagnose_sony_usb_state()}"
            )
        senser_auth = SonySenserAuthDevice(senser_dev.driver)
        senser_auth.start()
        senser_auth.authenticate()
        SonySenserCamera(senser_dev).setTerminalEnable(True)
        time.sleep(0.5)
        for _ in range(5):
            senser_dev.readTerminal()
            time.sleep(0.05)
        check_camera_model(senser_dev, force=force)
        return senser_dev

    raise RuntimeError(f"Unsupported Sony device state: {type(dev).__name__}\nUSB 诊断: {diagnose_sony_usb_state()}")


def upload_tmpbt(backend, sh, remote_path: str, data: bytes, executable: bool = False) -> None:
    validate_tmpbt_path(remote_path)
    validate_tmpbt_data(data)

    want_md5 = hashlib.md5(data).hexdigest()
    cur = sh(f"if [ -f {remote_path} ]; then /bin/busybox md5sum {remote_path}; fi")
    if not re.fullmatch(want_md5 + r"\s+" + re.escape(remote_path), cur.strip()):
        backend.writeFile(remote_path, io.BytesIO(data))
        got = sh(f"/bin/busybox md5sum {remote_path}")
        if not re.fullmatch(want_md5 + r"\s+" + re.escape(remote_path), got.strip()):
            raise RuntimeError(f"MD5 mismatch for {remote_path}")
    if executable:
        rc = sh(f"/bin/busybox chmod 700 {remote_path}; echo RC_$?")
        if "RC_0" not in rc:
            raise RuntimeError(f"chmod 700 failed: {rc!r}")


def read_camera_file_verified(backend, sh, remote_path: str) -> bytes:
    """Read a firmware/system file read-only via Senser USB and verify MD5."""
    md5_out = sh(f"/bin/busybox md5sum {remote_path}")
    m = re.search(rf"([0-9a-f]{{32}})\s+{re.escape(remote_path)}", md5_out)
    if not m:
        raise RuntimeError(f"无法获取相机文件 {remote_path} 的 MD5: {md5_out!r}")
    want_md5 = m.group(1)

    buf = io.BytesIO()
    backend.readFile(remote_path, buf)
    data = buf.getvalue()
    if not data:
        raise RuntimeError(f"从相机读取 {remote_path} 返回空数据")

    got_md5 = hashlib.md5(data).hexdigest()
    if got_md5 != want_md5:
        raise RuntimeError(
            f"读取 {remote_path} MD5 校验失败: 期望 {want_md5}, 实际 {got_md5}"
        )
    return data


def parse_map_rx_base(maps_text: str, suffix: str) -> int | None:
    for line in maps_text.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[-1].endswith(suffix) and parts[1] == "r-xp":
            return int(parts[0].split("-")[0], 16)
    return None


def get_verified_im_pid(sh, force: bool = False) -> int:
    raw = sh("/bin/busybox pidof im.elf").strip()
    tokens = [tok for tok in raw.split() if tok.isdigit()]
    if len(tokens) != 1:
        raise RuntimeError(f"异常的 im.elf PID 数量: {raw!r}")
    pid = int(tokens[0])
    if pid <= 0:
        raise RuntimeError(f"非法 PID: {pid}")
    comm = sh(f"/bin/busybox cat /proc/{pid}/comm 2>/dev/null || true").strip()
    if comm != "im.elf":
        raise RuntimeError(f"/proc/{pid}/comm 校验失败: 期望 'im.elf'，实际 {comm!r}")
    if pid != 158 and not force:
        raise RuntimeError(
            f"非预期的 im.elf PID={pid} (A7R3 v3.10 常规启动应为 158)，如需跳过请传 --force"
        )
    return pid


def check_tmpbt_space(sh, need_bytes: int) -> None:
    mounts = sh("/bin/busybox grep ' /tmp_bt ' /proc/mounts")
    if not re.search(r"^\S+\s+/tmp_bt\s+tmpfs\s", mounts, re.M):
        raise RuntimeError(f"/tmp_bt 不是 tmpfs 分区，拒绝继续以保护 Flash: {mounts!r}")
    sh("mount -o remount,size=16M /tmp_bt")
    df_out = sh("/bin/busybox df -k /tmp_bt 2>/dev/null || true")
    avail_kb = None
    for line in df_out.splitlines()[1:]:
        cols = line.split()
        if len(cols) >= 4 and cols[3].isdigit():
            avail_kb = int(cols[3])
            break
    need_kb = (need_bytes // 1024) + 1024
    if avail_kb is not None and avail_kb < need_kb:
        raise RuntimeError(
            f"/tmp_bt 可用内存空间不足: 剩余 {avail_kb} KB < 至少需要 {need_kb} KB"
        )


def parse_syscall_result(text: str) -> int:
    rc = re.search(r"(?:^|\s)SVC_RC_(\d+)\s*$", text)
    scalar = re.search(r"(?:^|\s)([a-f0-9]{8})\s+SVC_RC_0\s*$", text)
    if not rc or int(rc[1]) != 0 or not scalar:
        raise RuntimeError(f"Remote syscall failed: {text!r}")
    val = int(scalar[1], 16)
    if val >= 0xFFFFF001:
        raise OSError((1 << 32) - val, f"errno {(1 << 32) - val}")
    return val


def read_mem_bytes(sh, reader: str, addr: int, length: int) -> bytes:
    hex_str = sh(f"{reader} 0x{addr:x} {length}").strip().replace(" ", "").replace("\n", "")
    data = bytes.fromhex(hex_str)
    if len(data) != length:
        raise RuntimeError(f"Short read at {addr:#x}: {len(data)}/{length}")
    return data


def _ensure_dual_hud_runtime_bin(backend, sh) -> None:
    """Prepare & upload /tmp_bt/dual_hud_payload.bin from local cache, local uxc, or camera read-only."""
    from gen_dual_hud_payload import (
        CAMERA_BIN_PATH,
        CAMERA_UXC_PATH,
        DEFAULT_BIN_OUT,
        find_local_uxc,
        generate_runtime_bin,
        validate_runtime_bin,
    )

    bin_bytes: bytes | None = None
    if DEFAULT_BIN_OUT.is_file():
        try:
            bin_bytes = validate_runtime_bin(DEFAULT_BIN_OUT)
        except Exception:
            bin_bytes = None

    if bin_bytes is None:
        uxc_path = find_local_uxc()
        if uxc_path is not None:
            try:
                generate_runtime_bin(uxc_path.read_bytes(), DEFAULT_BIN_OUT)
                bin_bytes = validate_runtime_bin(DEFAULT_BIN_OUT)
                print(f"[+] 已从本地 {uxc_path.name} 生成运行时双显载荷: {DEFAULT_BIN_OUT.name}")
            except Exception as exc:
                print(f"[!] 警告: 本地 viewStlrec.uxc 未通过双显指纹校验 ({exc})")

    if bin_bytes is None:
        try:
            print(f"[*] 正在从相机只读获取 {CAMERA_UXC_PATH} 以生成运行时双显载荷...")
            uxc_bytes = read_camera_file_verified(backend, sh, CAMERA_UXC_PATH)
            generate_runtime_bin(uxc_bytes, DEFAULT_BIN_OUT)
            bin_bytes = validate_runtime_bin(DEFAULT_BIN_OUT)
            print(f"[+] 已缓存运行时双显载荷: {DEFAULT_BIN_OUT.name} ({len(bin_bytes)} bytes)")
        except Exception as exc:
            print(f"[!] 警告: 未能准备运行时双显载荷 ({exc})；水平仪与直方图同显将保持置灰透传。")
            sh(f"rm -f {CAMERA_BIN_PATH}")
            return

    upload_tmpbt(backend, sh, CAMERA_BIN_PATH, bin_bytes)


def cmd_deploy(backend, sh, so_path: Path, open_now: bool, force: bool = False) -> int:
    require_usb_runtime()
    from ram_loader.build_ram_tools import (
        build_dlopen_stub_bytes,
        build_remote_dlopen_elf,
        build_remote_svc_elf,
    )

    so_bytes = validate_arm32_shared_elf(so_path)

    sh("echo -n 'keepalive' > /sys/power/wake_lock 2>/dev/null || true")
    check_tmpbt_space(sh, len(so_bytes) + 4096)

    pid = get_verified_im_pid(sh, force=force)
    maps = sh(f"cat /proc/{pid}/maps")

    missing_libs = [lib for lib in REQUIRED_IM_LIBS if parse_map_rx_base(maps, lib) is None]
    if missing_libs and not force:
        raise RuntimeError(
            f"目标进程 im.elf (PID={pid}) 缺少预期固件映射库: {missing_libs}，请确认是否为 A7R3 v3.10"
        )

    libc_base = parse_map_rx_base(maps, "/libc-2.11.2.so")
    libdl_base = parse_map_rx_base(maps, "/libdl-2.11.2.so")
    if not libc_base or not libdl_base:
        raise RuntimeError("Missing libc-2.11.2.so or libdl-2.11.2.so in /proc/pid/maps")

    reader = "/tmp_bt/sdk_show_hex"
    writer = "/tmp_bt/sdk_mem_writer"
    upload_tmpbt(
        backend,
        sh,
        reader,
        build_show_hex_elf(source_path=f"/proc/{pid}/mem", max_bytes=256),
        executable=True,
    )
    upload_tmpbt(backend, sh, writer, build_mem_writer_elf(pid=pid), executable=True)
    svc_addr = libc_base + LIBC_SVC_OFF

    sh("rm -f /tmp_bt/addon_quit /tmp_bt/addon_off; : > /tmp_bt/addon_arm")
    if open_now:
        sh(": > /tmp_bt/addon_open")
    else:
        sh("rm -f /tmp_bt/addon_open")

    target_so = None
    for idx in range(1, 32):
        cand = "/tmp_bt/addon_ram.so" if idx == 1 else f"/tmp_bt/addon_ram_{idx}.so"
        if cand not in maps:
            target_so = cand
            break
    if target_so is None:
        raise RuntimeError(
            "本轮开机热加载槽位已耗尽 (addon_ram.so ~ addon_ram_31.so 均已在内存中映射)，"
            "为防止覆盖正在运行的代码页导致崩溃，请先重启相机再部署。"
        )

    _ensure_dual_hud_runtime_bin(backend, sh)
    upload_tmpbt(backend, sh, target_so, so_bytes, executable=True)
    mmap_elf = build_remote_svc_elf(pid, svc_addr, 192, (0, 0x10000, 7, 0x22, 0xFFFFFFFF, 0))
    upload_tmpbt(backend, sh, "/tmp_bt/sdk_mmap", mmap_elf, executable=True)
    alloc_base = parse_syscall_result(sh("/tmp_bt/sdk_mmap; echo SVC_RC_$?"))
    if alloc_base == 0 or (alloc_base & 0xFFF) != 0 or alloc_base >= 0xBF000000:
        raise RuntimeError(f"远程 mmap2 返回异常地址: {alloc_base:#010x}")

    try:
        stub_blob, _, res_addr, trap_addr = build_dlopen_stub_bytes(
            alloc_base, libdl_base + LIBDL_DLOPEN_OFF, libdl_base + LIBDL_DLERROR_OFF, target_so
        )
        upload_tmpbt(backend, sh, "/tmp_bt/sdk_stub.bin", stub_blob)
        if "WR_0" not in sh(f"{writer} 0x{alloc_base:x} /tmp_bt/sdk_stub.bin; echo WR_$?"):
            raise RuntimeError("mem_writer failed")
        cflush_elf = build_remote_svc_elf(pid, svc_addr, 0xF0002, (alloc_base, alloc_base + 0x1000, 0))
        upload_tmpbt(backend, sh, "/tmp_bt/sdk_cflush", cflush_elf, executable=True)
        parse_syscall_result(sh("/tmp_bt/sdk_cflush; echo SVC_RC_$?"))

        dl_elf = build_remote_dlopen_elf(pid, alloc_base, alloc_base + 0xF800, trap_addr)
        upload_tmpbt(backend, sh, "/tmp_bt/sdk_dlopen", dl_elf, executable=True)
        dl_out = sh("/tmp_bt/sdk_dlopen; echo DL_RC_$?", timeout=20)
        if "DL_RC_0" not in dl_out:
            raise RuntimeError(f"remote_dlopen failed: {dl_out!r}")
        handle, _ = struct.unpack("<II", read_mem_bytes(sh, reader, res_addr, 8))
        print(f"[+] dlopen({target_so}) -> handle={handle:#08x}")
        if handle == 0:
            raise RuntimeError("dlopen returned NULL")
    finally:
        try:
            munmap_elf = build_remote_svc_elf(pid, svc_addr, 91, (alloc_base, 0x10000))
            upload_tmpbt(backend, sh, "/tmp_bt/sdk_munmap", munmap_elf, executable=True)
            parse_syscall_result(sh("/tmp_bt/sdk_munmap; echo SVC_RC_$?"))
        except Exception as cleanup_exc:
            print(f"[!] 清理远程临时页 munmap 时警告 (可忽略): {cleanup_exc}", file=sys.stderr)

    time.sleep(0.8)
    print(sh("/bin/busybox tail -n 30 /tmp_bt/addon.log 2>/dev/null || echo '(no log)'"))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("action", nargs="?", default="deploy", choices=("deploy", "open", "status", "quit"))
    ap.add_argument("--so", type=Path, default=SDK_DIR / "build" / "demo_native_tab7_ram.so")
    ap.add_argument("--open", action="store_true", help="Open add-on card immediately after deploy")
    ap.add_argument("--force", action="store_true", help="Bypass PID/model/module safety checks")
    args = ap.parse_args()

    require_usb_runtime()
    if args.action == "deploy":
        validate_arm32_shared_elf(args.so)

    with open_usb_drivers() as driver:
        dev = ensure_service_terminal(driver, force=args.force)
        backend = SenserPlatformBackend(SonySenserCamera(dev))
        sh = lambda cmd, timeout=15: shell_command(dev, cmd, timeout=timeout)

        if args.action == "deploy":
            return cmd_deploy(backend, sh, args.so, args.open, force=args.force)
        if args.action == "open":
            sh(": > /tmp_bt/addon_open")
            time.sleep(0.3)
            print(sh("/bin/busybox tail -n 20 /tmp_bt/addon.log 2>/dev/null || true"))
            return 0
        if args.action == "status":
            print(sh("/bin/busybox pidof im.elf"))
            print(sh("/bin/busybox tail -n 30 /tmp_bt/addon.log 2>/dev/null || echo '(no log)'"))
            return 0
        if args.action == "quit":
            sh(": > /tmp_bt/addon_quit")
            time.sleep(0.5)
            print(sh("/bin/busybox tail -n 20 /tmp_bt/addon.log 2>/dev/null || true"))
            return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
