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
    importDriver,
    require_usb_runtime,
    shell_command,
    validate_arm32_shared_elf,
)

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
    driver_ctx = importDriver("libusb")
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
        print("  [NOTE] 未提供本地 a7r3_dual_hud_payload.local.h，水平仪与直方图同显处于透传模式。")


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

    llvm_env = os.environ.get("A7R3_LLVM_BIN", "").strip()
    llvm_dir = Path(llvm_env) if llvm_env else (Path(shutil.which("clang")).resolve().parent if shutil.which("clang") else None)
    llvm_ok = bool(llvm_dir and (llvm_dir / "clang.exe" if (llvm_dir / "clang.exe").exists() else llvm_dir / "clang").is_file())
    print(f"  [{'OK' if llvm_ok else 'WARN'}] LLVM 工具链 (clang): {llvm_dir if llvm_ok else '未找到，请配置 A7R3_LLVM_BIN'}")

    fw_env = os.environ.get("A7R3_FW_LIB_DIR", "").strip()
    fw_ok = bool(
        fw_env
        and (Path(fw_env) / "libc-2.11.2.so").is_file()
        and (Path(fw_env) / "libdl-2.11.2.so").is_file()
        and (Path(fw_env) / "libpthread-2.11.2.so").is_file()
    )
    print(f"  [{'OK' if fw_ok else 'WARN'}] A7R3 v3.10 系统库 (/lib): {fw_env if fw_ok else '未找到，请配置 A7R3_FW_LIB_DIR'}")

    for pkg, desc in (("PIL", "Pillow 字模生成"), ("keystone", "Keystone ARM 汇编"), ("pmca", "Sony-PMCA-RE USB 通信")):
        found = importlib.util.find_spec(pkg) is not None
        print(f"  [{'OK' if found else 'WARN'}] Python 包 {pkg} ({desc}): {'已就绪' if found else '未检测到'}")

    local_payload = SDK_DIR / "src" / "a7r3_dual_hud_payload.local.h"
    print(f"  [INFO] 可选本地 Dual-HUD 载荷: {'已配置 (.local.h)' if local_payload.is_file() else '未配置 (纯透传构建)'}")

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
    print("  [0] 退出控制台")
    print("-" * 64)


def interactive_loop(so_path: Path, force: bool = False) -> int:
    while True:
        print_banner(so_path)
        try:
            choice = input("请选择操作编号 [0-7]: ").strip()
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
        choices=("deploy", "status", "open", "quit", "build", "update", "check"),
        help="可选直接执行的子命令；不填则进入交互式菜单",
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
    except KeyboardInterrupt:
        print("\n[!] 操作已被用户中断。")
        return 130
    except Exception as exc:
        print(format_error(exc))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
