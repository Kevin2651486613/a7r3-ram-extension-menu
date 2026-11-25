#!/usr/bin/env python3
"""Sony ILCE-7RM3 (A7R3 v3.10) Pure-RAM Extension Menu Console Tool (Experimental)."""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
import datetime

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from env_config import load_local_env  # noqa: E402

load_local_env(SDK_DIR)

from deploy_ram import (  # noqa: E402
    SenserPlatformBackend,
    SonySenserCamera,
    cmd_deploy,
    ensure_service_terminal,
    get_verified_im_pid,
    open_usb_drivers,
    read_camera_file_verified,
    require_usb_runtime,
    shell_command,
    validate_arm32_shared_elf,
)
from build_addon import resolve_fw_lib_dir, resolve_llvm_bin, tool_exe  # noqa: E402

DEFAULT_SO = SDK_DIR / "build" / "demo_native_tab7_ram.so"
DEFAULT_SRC = SDK_DIR / "src" / "demo_native_tab7.c"
GEN_ASSETS = TOOLS_DIR / "gen_md3_assets.py"
BUILD_SCRIPT = TOOLS_DIR / "build_addon.py"

HOOK_CHECKS = (
    ("SDK_TAB7_ALIVE", "后台工作线程存活"),
    ("SDK_TAB7_TEXT_HOOK=0x1", "原生 TextSprite UTF-8 渲染钩子"),
    ("SDK_TAB7_MGR_HOOK=0x1", "CmnViewProcessDataMgr 纯内存虚表钩子"),
    ("SDK_TAB7_MLT_HOOK=0x1", "PAS_MenuListTab 第 7 大类排版钩子"),
    ("SDK_TAB7_INSTALLED=0x1", "原生第 7 大类设置树节点挂载"),
    ("SDK_EV_HOOKS=0x1", "曝光补偿波轮屏蔽钩子"),
    ("SDK_DUAL_HOOKS=0x1", "水平仪与直方图同显模块"),
    ("SDK_PB_HOOKS=0x1", "相册显示焦点位置钩子"),
    ("SDK_SFX_INSTALL_OK", "自定义电子快门音效钩子"),
    ("SDK_MTOUCH_HOOKS=0x1", "原生菜单触摸控制钩子"),
)

RESTORE_MARKERS = (
    "SDK_TAB7_RESTORED=",
    "SDK_MTOUCH_RESTORED=",
    "SDK_CERT_RESTORED=",
    "SDK_SFX_RESTORED=",
    "SDK_PB_RESTORED=",
    "SDK_DUAL_RESTORED=",
    "SDK_OSAL_RESTORED=",
    "SDK_IOCTL_RESTORED=",
)


def configure_utf8_stdio() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconf = getattr(stream, "reconfigure", None)
        if callable(reconf):
            try:
                reconf(encoding="utf-8", errors="replace")
            except Exception:
                pass


def validate_runtime_env() -> None:
    if sys.version_info < (3, 10):
        raise RuntimeError(f"需要 Python 3.10+，当前版本为 {sys.version.split()[0]}")
    for p in (GEN_ASSETS, BUILD_SCRIPT, DEFAULT_SRC):
        if not p.is_file():
            raise FileNotFoundError(f"缺少必要工程文件: {p}")


def validate_so_path(so_path: Path) -> None:
    validate_arm32_shared_elf(so_path)


def open_service_session(force: bool = False):
    require_usb_runtime()
    driver_ctx = open_usb_drivers()
    driver = driver_ctx.__enter__()
    try:
        dev = ensure_service_terminal(driver, force=force)
        backend = SenserPlatformBackend(SonySenserCamera(dev))
        sh = lambda cmd, timeout=15: shell_command(dev, cmd, timeout=timeout)
        return driver_ctx, backend, sh
    except Exception:
        driver_ctx.__exit__(None, None, None)
        raise


def get_im_pid(sh, force: bool = False) -> int:
    last_err: Exception | None = None
    for _ in range(3):
        try:
            return get_verified_im_pid(sh, force=force)
        except Exception as exc:
            last_err = exc
            time.sleep(0.3)
    raise RuntimeError(f"未检测到有效的相机主进程 im.elf ({last_err})")


def read_log(sh, tail_lines: int = 0) -> str:
    if tail_lines > 0:
        cmd = f"/bin/busybox tail -n {tail_lines} /tmp_bt/addon.log 2>/dev/null || true"
    else:
        cmd = "/bin/busybox cat /tmp_bt/addon.log 2>/dev/null || true"
    return sh(cmd)


def is_addon_running(log_text: str) -> bool:
    last_alive = max(log_text.rfind("SDK_TAB7_ALIVE"), log_text.rfind("SDK_CERT_ALIVE"))
    if last_alive < 0:
        return False
    last_quit = max(log_text.rfind("SDK_TAB7_QUIT"), log_text.rfind("SDK_CERT_QUIT"))
    return last_alive > last_quit


def current_boot_segment(log_text: str) -> str:
    idx = max(log_text.rfind("SDK_TAB7_ALIVE"), log_text.rfind("SDK_CERT_ALIVE"))
    return log_text[idx:] if idx >= 0 else log_text


def print_hook_summary(log_text: str) -> None:
    running = is_addon_running(log_text)
    seg = current_boot_segment(log_text)
    print("\n--- 功能状态检查 ---")
    print(f"  运行状态: {'[运行中]' if running else '[已停止 / 未加载]'}")
    for token, desc in HOOK_CHECKS:
        ok = token in seg
        mark = "[OK]" if ok else "[--]"
        print(f"  {mark} {desc} ({token})")
    if "SDK_DUAL_NO_LOCAL_PAYLOAD" in seg:
        print(
            "  [NOTE] 未提供本地 a7r3_dual_hud_payload.local.h，水平仪与直方图同显已置灰禁用（纯透传）。\n"
            "         连接相机后运行 `python tools/a7r3_addon_console.py gen-payload --from-camera`\n"
            "         或指定本地 viewStlrec.uxc 生成后执行 `update` 即可启用。"
        )


def safe_quit(sh, timeout_s: float = 4.0) -> bool:
    log_text = read_log(sh)
    if not is_addon_running(log_text):
        print("[*] 当前没有正在运行的拓展菜单实例，无需卸载。")
        return False

    print("[*] 正在发送安全卸载信号 (/tmp_bt/addon_quit)...")
    sh(": > /tmp_bt/addon_quit")
    deadline = time.time() + timeout_s
    latest_log = log_text
    while time.time() < deadline:
        time.sleep(0.25)
        latest_log = read_log(sh, tail_lines=40)
        if not is_addon_running(latest_log) and (
            "SDK_TAB7_QUIT" in latest_log or "SDK_CERT_QUIT" in latest_log
        ):
            break

    if is_addon_running(latest_log):
        raise RuntimeError(
            f"等待 {timeout_s:.1f}s 后旧实例仍未确认退出 (未检测到退出标记)。"
            "为防止重复挂钩造成相机崩溃，已中止部署；请重启相机后再试。"
        )

    print("[+] 已安全卸载并恢复全部原生钩子:")
    for line in latest_log.splitlines()[-15:]:
        if any(m in line for m in RESTORE_MARKERS) or "SDK_TAB7_QUIT" in line or "SDK_CERT_QUIT" in line:
            print(f"    {line}")
    return True


def format_error(exc: Exception) -> str:
    msg = str(exc)
    low = msg.lower()
    if "dl_rc_25" in low:
        return (
            "[!!] 致命加载错误 (DL_RC_25): dlopen 失败。\n"
            "     禁止在同一开机周期内重试！请先关闭相机电源（或重启相机）后再重新部署。"
        )
    if "usb 诊断:" in low or "usb连接" in low:
        return f"[!] {msg}"
    if "no sony device found" in low:
        return "[!] 未检测到 Sony 相机：请确认相机已开机、USB 线已连接且处于大容量存储 (MSC) 或服务模式。"
    if "timed out waiting for service mode" in low or "timeout" in low or "libusb" in low:
        return f"[!] USB 通信超时或断开，请检查数据线或重新插拔后再试。\n     原始信息: {msg}"
    return f"[!] 操作失败: {msg}"


def action_check(so_path: Path) -> int:
    print("=== 离线环境与边界安全自检 ===")
    ok_all = True

    py_ok = sys.version_info >= (3, 10)
    print(f"  [{'OK' if py_ok else 'FAIL'}] Python 版本: {sys.version.split()[0]} (要求 >= 3.10)")
    ok_all &= py_ok

    env_local = SDK_DIR / ".env.local"
    print(f"  [{'OK' if env_local.is_file() else 'INFO'}] 本地环境配置 .env.local: {'已加载' if env_local.is_file() else '未创建 (将使用系统环境变量)'}")

    llvm_dir = resolve_llvm_bin(None)
    llvm_ok = bool(
        llvm_dir
        and tool_exe(llvm_dir, "clang").is_file()
        and tool_exe(llvm_dir, "ld.lld").is_file()
        and tool_exe(llvm_dir, "llvm-readelf").is_file()
    )
    print(
        f"  [{'OK' if llvm_ok else 'WARN'}] LLVM 工具链 (clang + ld.lld + llvm-readelf): "
        f"{llvm_dir if llvm_ok else '未找到完整 LLVM (Windows 请装 LLVM；macOS 请 brew install llvm；Linux 请装 clang lld llvm)'}"
    )

    fw_dir = resolve_fw_lib_dir(None)
    fw_ok = bool(
        fw_dir
        and (fw_dir / "libc-2.11.2.so").is_file()
        and (fw_dir / "libdl-2.11.2.so").is_file()
        and (fw_dir / "libpthread-2.11.2.so").is_file()
    )
    print(
        f"  [{'OK' if fw_ok else 'WARN'}] A7R3 v3.10 系统库 (/lib): "
        f"{fw_dir if fw_ok else '未找到，连接相机运行 `python tools/a7r3_addon_console.py init-camera` 可自动提取'}"
    )

    for pkg, desc in (
        ("PIL", "Pillow 字模生成"),
        ("keystone", "Keystone ARM 汇编"),
        ("usb", "PyUSB / libusb 驱动"),
        ("pmca", "Sony-PMCA-RE (A7R3_PMCA_DIR)"),
    ):
        found = importlib.util.find_spec(pkg) is not None
        extra_note = ""
        if found and pkg == "usb":
            try:
                import usb.backend.libusb0 as l0
                import usb.backend.libusb1 as l1

                has_native = (l1.get_backend() is not None) or (l0.get_backend() is not None)
            except Exception:
                has_native = False
            if not has_native:
                found = False
                if sys.platform == "win32":
                    extra_note = " — 未检测到 libusb 原生驱动，请用 Zadig 为相机绑定 libusb-win32 / libusbK"
                elif sys.platform == "darwin":
                    extra_note = " — 未检测到 libusb 原生库，请运行 `brew install libusb`"
                else:
                    extra_note = " — 未检测到 libusb 原生库，请安装 `libusb-1.0-0` 并配置 udev 权限"
        if not found and pkg == "pmca":
            extra_note = " — 请运行 `git clone https://github.com/ma1co/Sony-PMCA-RE.git ../Sony-PMCA-RE`"
        status_str = "已就绪" if found else ("未检测到" + extra_note if not extra_note else extra_note.lstrip(" — "))
        print(f"  [{'OK' if found else 'WARN'}] Python 包 {pkg} ({desc}): {status_str}")

    try:
        from ram_loader.terminal_shell import shell_command as _sc  # noqa: F401
        from ram_loader.build_mem_writer import build_mem_writer_elf as _mw  # noqa: F401
        from ram_loader.build_show_hex import build_show_hex_elf as _sh  # noqa: F401
        helpers_ok = True
        helpers_msg = "已内置 (MIT)"
    except Exception as exc:
        helpers_ok = False
        helpers_msg = f"加载失败 ({exc})"
    print(f"  [{'OK' if helpers_ok else 'WARN'}] 内置注入助手 (terminal_shell / mem_writer / show_hex): {helpers_msg}")

    local_payload = SDK_DIR / "src" / "a7r3_dual_hud_payload.local.h"
    if local_payload.is_file():
        payload_msg = "已配置 (.local.h)"
    else:
        try:
            from gen_dual_hud_payload import find_local_uxc

            found_uxc = find_local_uxc(fw_dir)
        except Exception:
            found_uxc = None
        if found_uxc is not None:
            payload_msg = f"未生成，但已检测到 {found_uxc.name} (编译或运行 gen-payload 时将自动生成)"
        else:
            payload_msg = "未配置 (水平仪直方图同显将置灰；连接相机运行 `gen-payload --from-camera` 可生成)"
    print(f"  [INFO] 可选本地 Dual-HUD 载荷: {payload_msg}")

    if so_path.exists():
        try:
            validate_so_path(so_path)
            print(f"  [OK] 插件产物 ELF32 ARM 校验通过: {so_path.name} ({so_path.stat().st_size} bytes)")
        except Exception as exc:
            print(f"  [FAIL] 插件产物校验失败: {exc}")
            ok_all = False
    else:
        print(f"  [INFO] 插件产物尚未编译: {so_path}")

    return 0 if ok_all else 1


def action_deploy(so_path: Path, force: bool = False) -> int:
    validate_runtime_env()
    validate_so_path(so_path)

    print(f"[*] 准备纯内存部署: {so_path.name} ({so_path.stat().st_size} bytes)")
    print("[*] 安全模式: 仅写入 /tmp_bt (tmpfs) 与 im.elf 内存，零 NAND Flash 写入。")
    driver_ctx, backend, sh = open_service_session(force=force)
    with driver_ctx:
        pid = get_im_pid(sh, force=force)
        print(f"[+] 相机主机进程 im.elf PID={pid}")
        if is_addon_running(read_log(sh)):
            print("[*] 检测到旧版实例正在运行，先执行安全无损卸载...")
            safe_quit(sh)
            time.sleep(0.3)

        rc = cmd_deploy(backend, sh, so_path, open_now=False, force=force)
        time.sleep(0.3)
        log_text = read_log(sh)
        print_hook_summary(log_text)
        print("\n[+] 部署完成! 使用方式: 相机按 MENU -> 顶栏第 7 个大类 (拓展菜单)")
        return rc


def action_status(force: bool = False) -> int:
    driver_ctx, _, sh = open_service_session(force=force)
    with driver_ctx:
        pid = get_im_pid(sh, force=force)
        print(f"[+] 相机已连接，主进程 im.elf PID={pid}")
        full_log = read_log(sh)
        if not full_log.strip():
            print("[*] 当前 /tmp_bt/addon.log 为空（本轮开机尚未部署过插件）。")
            return 0
        print_hook_summary(full_log)
        tail = "\n".join(full_log.splitlines()[-20:])
        print("\n--- 最近日志 (tail 20) ---")
        print(tail)
        return 0


def action_open(force: bool = False) -> int:
    driver_ctx, _, sh = open_service_session(force=force)
    with driver_ctx:
        pid = get_im_pid(sh, force=force)
        log_text = read_log(sh)
        if not is_addon_running(log_text):
            print(f"[!] 相机主进程 PID={pid} 在线，但拓展菜单尚未部署或已卸载，请先部署。")
            return 1
        sh(": > /tmp_bt/addon_open")
        time.sleep(0.3)
        print("[+] 已触发打开相机屏幕上的 MD3 拓展菜单。")
        return 0


def action_quit(force: bool = False) -> int:
    driver_ctx, _, sh = open_service_session(force=force)
    with driver_ctx:
        pid = get_im_pid(sh, force=force)
        print(f"[+] 相机已连接，主进程 im.elf PID={pid}")
        safe_quit(sh)
        return 0


def action_build(so_path: Path) -> int:
    validate_runtime_env()
    src_text = DEFAULT_SRC.read_text(encoding="utf-8", errors="replace")
    if "md3_demo_labels.h" in src_text:
        print("[*] 步骤 1/2: 生成 MD3 中文点阵字模...")
        res1 = subprocess.run([sys.executable, str(GEN_ASSETS)], capture_output=False)
        if res1.returncode != 0:
            print(f"[!] 字模生成失败 (exit={res1.returncode})")
            return res1.returncode
        print("[*] 步骤 2/2: 交叉编译 ARMv7 纯内存插件 (.so)...")
    else:
        print("[*] 交叉编译原生第 7 大类 (Route A1) 纯内存插件 (.so)...")

    res2 = subprocess.run(
        [sys.executable, str(BUILD_SCRIPT), "--src", str(DEFAULT_SRC), "--out", str(so_path)],
        capture_output=False,
    )
    if res2.returncode != 0:
        print(f"[!] 编译失败 (exit={res2.returncode})")
        return res2.returncode

    validate_so_path(so_path)
    print(f"[+] 编译成功: {so_path} ({so_path.stat().st_size} bytes)")
    return 0


def action_gen_payload(from_camera: bool = False, force: bool = False) -> int:
    from gen_dual_hud_payload import (
        CAMERA_UXC_PATH,
        DEFAULT_OUT,
        find_local_uxc,
        generate_local_header,
        pull_uxc_from_camera,
    )

    if not from_camera:
        uxc_path = find_local_uxc()
        if uxc_path is not None:
            out = generate_local_header(uxc_path.read_bytes(), out_path=DEFAULT_OUT)
            print(f"[+] 已从本地固件文件生成双显载荷: {out} (来源: {uxc_path})")
            return 0
        print("[*] 本地未检测到 viewStlrec.uxc，尝试从已连接相机只读读取...")

    print(f"[*] 正在从相机只读获取 {CAMERA_UXC_PATH} (零闪存写入)...")
    uxc_bytes = pull_uxc_from_camera(force=force)
    out = generate_local_header(uxc_bytes, out_path=DEFAULT_OUT)
    print(f"[+] 已从相机本机生成双显载荷: {out}")
    print("[*] 请执行 `[6] 一键编译并热更新部署` (update) 使同显功能生效。")
    return 0


def action_init_camera(force: bool = False) -> int:
    """Pull 3 /lib linking libraries + viewStlrec.uxc read-only from connected A7R3."""
    from gen_dual_hud_payload import CAMERA_UXC_PATH, DEFAULT_OUT, generate_local_header

    out_lib_dir = SDK_DIR / "firmware" / "lib"
    out_lib_dir.mkdir(parents=True, exist_ok=True)
    lib_files = (
        "/lib/libc-2.11.2.so",
        "/lib/libdl-2.11.2.so",
        "/lib/libpthread-2.11.2.so",
    )

    print("[*] 正在连接相机只读初始化本地开发环境（零闪存写入，产出均位于 .gitignore 目录）...")
    driver_ctx, backend, sh = open_service_session(force=force)
    with driver_ctx:
        for remote_p in lib_files:
            data = read_camera_file_verified(backend, sh, remote_p)
            dst = out_lib_dir / Path(remote_p).name
            dst.write_bytes(data)
            print(f"[+] 已保存 {dst} ({len(data)} bytes)")

        uxc_bytes = read_camera_file_verified(backend, sh, CAMERA_UXC_PATH)
        out_h = generate_local_header(uxc_bytes, out_path=DEFAULT_OUT)
        print(f"[+] 已生成本地双显载荷: {out_h}")

    print("[+] 本地开发环境初始化完成！现在可以直接运行 `check` / `build` / `update`。")
    return 0


def action_update(so_path: Path, force: bool = False) -> int:
    rc = action_build(so_path)
    if rc != 0:
        return rc
    print("")
    return action_deploy(so_path, force=force)


def print_banner(so_path: Path) -> None:
    if so_path.exists():
        st = so_path.stat()
        mtime = datetime.datetime.fromtimestamp(st.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
        so_info = f"就绪 ({st.st_size} bytes, 更新于 {mtime})"
    else:
        so_info = "未编译 (需先选 5 或 6)"

    print("=" * 64)
    print("  Sony ILCE-7RM3 (A7R3 v3.10) 原生第 7 大类拓展菜单控制台 (Route A1)")
    print("  [测试开发版] 存在潜在不稳定风险 · 仅写入 /tmp_bt 与 RAM")
    print("=" * 64)
    print(f"  目标插件: {so_path.name} -> {so_info}")
    print("-" * 64)
    print("  [1] 一键部署到相机内存 (如已运行则先自动卸载旧版)")
    print("  [2] 查看相机连接与运行状态 / 日志")
    print("  [3] 立即弹出叠加菜单 (仅 MD3 模式有效)")
    print("  [4] 安全卸载插件并恢复原生钩子 (不重启相机)")
    print("  [5] 重新编译插件 (.so)")
    print("  [6] 一键编译并热更新部署到相机")
    print("  [7] 环境与边界安全自检 (离线检查，无需连接相机)")
    print("  [8] 生成本地双显载荷 (a7r3_dual_hud_payload.local.h)")
    print("  [9] 从相机只读初始化编译依赖 (/lib 三件套 + 双显载荷)")
    print("  [0] 退出控制台")
    print("-" * 64)


def interactive_loop(so_path: Path, force: bool = False) -> int:
    while True:
        print_banner(so_path)
        try:
            choice = input("请选择操作编号 [0-9]: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n已退出。")
            return 0

        if not choice:
            continue
        if choice == "0":
            print("已退出。")
            return 0

        print("")
        try:
            if choice == "1":
                action_deploy(so_path, force=force)
            elif choice == "2":
                action_status(force=force)
            elif choice == "3":
                action_open(force=force)
            elif choice == "4":
                action_quit(force=force)
            elif choice == "5":
                action_build(so_path)
            elif choice == "6":
                action_update(so_path, force=force)
            elif choice == "7":
                action_check(so_path)
            elif choice == "8":
                action_gen_payload(force=force)
            elif choice == "9":
                action_init_camera(force=force)
            else:
                print(f"[!] 无效选项: {choice!r}")
        except KeyboardInterrupt:
            print("\n[!] 操作已被用户中断。")
        except Exception as exc:
            print(format_error(exc))

        print("")
        try:
            input("按回车键返回主菜单...")
        except (EOFError, KeyboardInterrupt):
            print("\n已退出。")
            return 0


def main() -> int:
    configure_utf8_stdio()
    ap = argparse.ArgumentParser(
        description="Sony A7R3 (v3.10) 纯内存拓展菜单控制台程序 (测试开发版)"
    )
    ap.add_argument(
        "action",
        nargs="?",
        choices=("deploy", "status", "open", "quit", "build", "update", "check", "gen-payload", "init-camera"),
        help="可选直接执行的子命令；不填则进入交互式菜单",
    )
    ap.add_argument(
        "--from-camera",
        action="store_true",
        help="配合 gen-payload 使用：直接从已连接相机只读读取 viewStlrec.uxc",
    )
    ap.add_argument(
        "--so",
        type=Path,
        default=DEFAULT_SO,
        help=f"插件 .so 文件路径 (默认: {DEFAULT_SO})",
    )
    ap.add_argument(
        "--force",
        action="store_true",
        help="跳过部分机身型号/PID/库检查（仅限高级开发者调试）",
    )
    args = ap.parse_args()

    try:
        validate_runtime_env()
    except Exception as exc:
        print(format_error(exc))
        return 1

    if args.action is None:
        return interactive_loop(args.so, force=args.force)

    try:
        if args.action == "check":
            return action_check(args.so)
        if args.action == "deploy":
            return action_deploy(args.so, force=args.force)
        if args.action == "status":
            return action_status(force=args.force)
        if args.action == "open":
            return action_open(force=args.force)
        if args.action == "quit":
            return action_quit(force=args.force)
        if args.action == "build":
            return action_build(args.so)
        if args.action == "update":
            return action_update(args.so, force=args.force)
        if args.action == "gen-payload":
            return action_gen_payload(from_camera=args.from_camera, force=args.force)
        if args.action == "init-camera":
            return action_init_camera(force=args.force)
    except KeyboardInterrupt:
        print("\n[!] 操作已被用户中断。")
        return 130
    except Exception as exc:
        print(format_error(exc))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
