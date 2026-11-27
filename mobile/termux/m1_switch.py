#!/usr/bin/env python3
"""A7R3 mobile injector - Step 1: Mass Storage -> Service Mode.

Runs inside ``termux-usb`` and receives the USB file descriptor as argv[1].

This step performs two USB operations only:
  1. one vendor control request (Sony "StartSenser"),
  2. the 3-round Senser authentication handshake on the bulk endpoints.

Nothing is written to the camera and no flash is touched; the camera
re-enumerates as 054C:0336 (Service Mode) when the handshake succeeds.

Reuses the Sony keys and the deliberately-buggy SHA-1 from the vendored
pmca tree, so the protocol is byte-for-byte identical to the desktop tool.
"""
import hashlib
import importlib.util
import struct
import sys
import time
from pathlib import Path

import usb1

ROOT = Path(__file__).resolve().parents[2]
PMCA_USB = ROOT / "tools" / "vendor" / "pmca" / "usb"

SENSER_PID = 0x0336
MSC_PID = 0x0BFF
AUTH_PACKET = 516  # cmd(2) + salt(2) + data(512)


def _load(name, filename):
    spec = importlib.util.spec_from_file_location(name, str(PMCA_USB / filename))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


constants = _load("pmca_constants", "constants.py")
crypto = _load("pmca_crypto", "crypto.py")


def main(fd):
    ctx = usb1.USBContext(with_device_discovery=False)
    handle = ctx.wrapSysDevice(fd)
    dev = handle.getDevice()
    pid = dev.getProductID()
    print("M1 pid 0x%04x" % pid)

    if pid == SENSER_PID:
        print("ALREADY_SERVICE: camera is already in Service Mode, run m2/inject instead")
        return 0
    if pid != MSC_PID:
        print("UNEXPECTED_PID: expected 0x0bff (Mass Storage)")
        return 1

    try:
        handle.detachKernelDriver(0)
    except Exception as exc:
        print("detach:", exc)

    try:
        if handle.getConfiguration() != 1:
            handle.setConfiguration(1)
    except Exception as exc:
        print("setConfiguration:", exc)

    try:
        handle.claimInterface(0)
    except Exception as exc:
        print("claim_err:", exc)
        return 1

    ep_out, ep_in = _bulk_endpoints(dev)
    print("endpoints", ep_out, ep_in)
    if ep_out is None or ep_in is None:
        print("EP_FAIL")
        return 1

    def write(data):
        _retry(lambda: handle.bulkWrite(ep_out, data, timeout=5000), handle, ep_out)

    def read(size):
        return _retry(lambda: bytes(handle.bulkRead(ep_in, size, timeout=5000)), handle, ep_in)

    def packet(cmd, data=b""):
        write(struct.pack(">HH", (~cmd) & 0xffff, 0) + data.ljust(512, b"\0"))
        resp = read(AUTH_PACKET)
        got, salt = struct.unpack(">HH", resp[:4])
        return ((~got) & 0xffff) - salt, resp[4:]

    try:
        handle.controlWrite(0x43, 1, 0x37ff, 0xd7aa, b"")
    except Exception as exc:
        print("start_err", exc)  # expected STALL, ignored like pmca does

    for i in range(3):
        ret, data = packet(1)
        print("R1", ret)
        if ret not in (2, 8):
            print("AUTH_REJECT", ret)
            return 2
        keys = constants.senserKeysSha1 if ret == 2 else constants.senserKeysSha256
        data = (data + keys[i]) if pid == SENSER_PID else data[:4]
        digest = crypto.sha1_faulty(data) if ret == 2 else hashlib.sha256(data).digest()
        ret, _ = packet(3, b"\1" + digest)
        print("R3", ret)
        if ret != 4:
            print("AUTH_REJECT3", ret)
            return 3

    ret, data = packet(5)
    print("R5", ret, list(data[:2]))
    print("AUTH_DONE")
    return 0


def _bulk_endpoints(dev):
    ep_out = ep_in = None
    for cfg in dev.iterConfigurations():
        for itf in cfg.iterInterfaces():
            for setting in itf.iterSettings():
                for ep in setting.iterEndpoints():
                    if (ep.getAttributes() & 3) != 2:
                        continue
                    addr = ep.getAddress()
                    if addr & 0x80:
                        ep_in = ep_in if ep_in is not None else addr
                    else:
                        ep_out = ep_out if ep_out is not None else addr
    return ep_out, ep_in


def _retry(fn, handle, ep, attempts=3):
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
    raise last


if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1])))
