#!/usr/bin/env python3
"""A7R3 mobile injector - Step 2: pure-RAM deploy into im.elf.

Runs inside ``termux-usb`` against the Service Mode device (054C:0336) and
receives the USB file descriptor as argv[1].

Only two things are adapted for Android:
  * a pyusb-compatible backend implemented on python-libusb1,
  * a Cryptodome.Hash.SHA256 stub backed by hashlib.
Everything else - /tmp_bt mounting, PID check, mmap2, cacheflush, dlopen,
hook verification - is the untouched desktop code from tools/deploy_ram.py.
"""
import hashlib
import os
import sys
import time
import types
from pathlib import Path

import usb1

ROOT = Path(__file__).resolve().parents[2]
M2_DIR = Path(__file__).resolve().parent

sys.path.insert(0, str(M2_DIR))          # LLVM-backed keystone shim
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "vendor"))


def _install_sha256_stub():
    """deploy path only needs SHA256.new(data).digest(); hashlib is enough."""
    pkg = types.ModuleType("Cryptodome")
    hashpkg = types.ModuleType("Cryptodome.Hash")
    sha = types.ModuleType("Cryptodome.Hash.SHA256")

    class _Digest:
        def __init__(self, data=b""):
            self._h = hashlib.sha256(data)

        def update(self, data):
            self._h.update(data)
            return self

        def digest(self):
            return self._h.digest()

        def hexdigest(self):
            return self._h.hexdigest()

    sha.new = lambda data=b"": _Digest(data)
    hashpkg.SHA256 = sha
    pkg.Hash = hashpkg
    sys.modules["Cryptodome"] = pkg
    sys.modules["Cryptodome.Hash"] = hashpkg
    sys.modules["Cryptodome.Hash.SHA256"] = sha


_install_sha256_stub()

import deploy_ram  # noqa: E402
from pmca.platform.backend.senser import SenserPlatformBackend  # noqa: E402
from pmca.usb.driver.generic import GenericUsbDriver, GenericUsbException  # noqa: E402
from pmca.usb.sony import SonySenserAuthDevice, SonySenserCamera, SonySenserDevice  # noqa: E402
from ram_loader.terminal_shell import shell_command  # noqa: E402

# The desktop helper expects raw libusb discovery; the fd is already wrapped.
deploy_ram.require_usb_runtime = lambda: None

SENSER_PID = 0x0336


class _Endpoint:
    """Mimic pyusb endpoint objects expected by pmca's GenericUsbDriver."""

    def __init__(self, address):
        self.bEndpointAddress = address
        self.bmAttributes = 2  # bulk


class LibusbFdBackend:
    """pmca USB backend on top of a termux-usb file descriptor."""

    def __init__(self, handle, endpoints, vid, pid):
        self.h = handle
        self.endpoints = endpoints
        self.vid = vid
        self.pid = pid

    def getEndpoints(self):
        return self.endpoints

    def reset(self):
        pass

    def getId(self):
        return self.vid, self.pid

    def read(self, ep, length, timeout=None):
        return _transfer(lambda: bytes(self.h.bulkRead(ep, length, timeout or 5000)), self.h, ep)

    def write(self, ep, data):
        _transfer(lambda: self.h.bulkWrite(ep, data, timeout=5000), self.h, ep)

    def classInterfaceRequestOut(self, request, value, index, data=b""):
        try:
            self.h.controlWrite(0x21, request, value, index, data)
        except Exception:
            raise GenericUsbException()

    def vendorRequestOut(self, request, value, index, data=b""):
        try:
            self.h.controlWrite(0x43, request, value, index, data)
        except Exception:
            raise GenericUsbException()


def _transfer(fn, handle, ep, attempts=3):
    """Retry once the endpoint has been cleared - a STALL otherwise sticks."""
    last = None
    for _ in range(attempts):
        try:
            return fn()
        except Exception as exc:
            last = exc
            try:
                handle.clearHalt(ep)
            except Exception:
                pass
            time.sleep(0.05)
    raise GenericUsbException(str(last))


def main(fd):
    ctx = usb1.USBContext(with_device_discovery=False)
    handle = ctx.wrapSysDevice(fd)
    dev = handle.getDevice()
    pid = dev.getProductID()
    print("STG1 pid 0x%04x" % pid)
    if pid != SENSER_PID:
        print("WRONG_DEVICE: not a Service Mode device - run the switch step first")
        return 9

    try:
        handle.detachKernelDriver(0)
    except Exception as exc:
        print("  detach", exc)
    try:
        cfg = handle.getConfiguration()
    except Exception:
        cfg = 0
    print("  cfg", cfg)
    if cfg != 1:
        try:
            handle.setConfiguration(1)
        except Exception as exc:
            print("  setcfg", exc)
    try:
        handle.claimInterface(0)
    except Exception as exc:
        print("  claim_err", exc)
        return 1

    endpoints = []
    for cfg_desc in dev.iterConfigurations():
        for itf in cfg_desc.iterInterfaces():
            for setting in itf.iterSettings():
                for ep in setting.iterEndpoints():
                    if (ep.getAttributes() & 3) == 2:
                        endpoints.append(_Endpoint(ep.getAddress()))
    print("STG2 eps", [e.bEndpointAddress for e in endpoints])

    backend = LibusbFdBackend(handle, endpoints, dev.getVendorID(), pid)
    driver = GenericUsbDriver(backend)
    device = SonySenserDevice(driver)
    print("STG3 device ok")

    ready = False
    try:
        for _ in range(2):
            try:
                device.readTerminal()
            except Exception as exc:
                print("  probe_read", type(exc).__name__)
            time.sleep(0.05)
        out = shell_command(device, "echo PING", timeout=4)
        print("  probe_out", repr(out[:40]))
        ready = True
    except Exception as exc:
        print("  probe_fail", type(exc).__name__, exc)

    if not ready:
        print("STG4 auth")
        auth = SonySenserAuthDevice(driver)
        try:
            auth.start()
        except Exception as exc:
            print("  start", exc)
        auth.authenticate()
        print("  auth_ok")
        SonySenserCamera(device).setTerminalEnable(True)
        print("  term_enabled")
        time.sleep(0.5)
        try:
            for _ in range(2):
                device.readTerminal()
        except Exception as exc:
            print("  drain", type(exc).__name__)

    print("STG5 TERMINAL_READY")
    print("  pid_im", repr(shell_command(device, "/bin/busybox pidof im.elf", timeout=5)))

    def sh(cmd, timeout=15):
        return shell_command(device, cmd, timeout=timeout)

    camera_backend = SenserPlatformBackend(SonySenserCamera(device))
    so_path = ROOT / "prebuilt" / "demo_native_tab7_ram.so"
    print("STG6 so", so_path.stat().st_size)
    rc = deploy_ram.cmd_deploy(camera_backend, sh, so_path, False)
    print("DEPLOY_RC", rc)
    return rc


if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1])))
