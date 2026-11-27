#!/usr/bin/env python3
"""A7R3 mobile injector - one-command entry point.

Scans USB devices, puts the camera into Service Mode if needed, waits for the
054C:0336 re-enumeration and then deploys the plugin into im.elf.

Usage:  inject
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
M1 = HERE / "m1_switch.py"
M2 = HERE / "m2_deploy.py"


def devices():
    try:
        proc = subprocess.run(["termux-usb", "-l"], capture_output=True,
                              text=True, timeout=20)
    except Exception:
        return []
    return re.findall(r"/dev/bus/usb/\d+/\d+", proc.stdout or "")


def run(device, script, timeout=900):
    print("\n>>> %s @ %s" % (script.name, device), flush=True)
    try:
        return subprocess.run(["termux-usb", "-r", "-e",
                               "python3 " + str(script), device],
                              timeout=timeout).returncode
    except Exception as exc:
        print("launch failed:", exc)
        return -1


def deployed(device):
    return run(device, M2) == 0


def main():
    print("=== A7R3 pure-RAM injector (Termux, no root) ===")
    found = devices()
    if not found:
        print("no USB device: check camera is on, USB connection = Mass Storage,")
        print("and that the OTG cable is plugged in.")
        return 1
    print("devices:", " ".join(found))

    if len(found) >= 2:
        print("multiple devices: camera already in Service Mode, deploying")
        for dev in found:
            if deployed(dev):
                print("\n[OK] press MENU on the camera -> 7th tab")
                return 0
        return 1

    before = set(found)
    run(found[0], M1)
    print("\nwaiting for 054C:0336 ...", flush=True)
    targets = None
    for _ in range(25):
        time.sleep(1)
        new = [d for d in devices() if d not in before]
        if new:
            targets = new
            break
    if targets is None:
        targets = devices()

    for dev in targets:
        if deployed(dev):
            print("\n[OK] press MENU on the camera -> 7th tab")
            return 0
    print("\n[FAILED] power-cycle the camera and try again")
    return 1


if __name__ == "__main__":
    sys.exit(main())
