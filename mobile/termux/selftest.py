#!/usr/bin/env python3
"""自检：确认 LLVM 垫片与真实 keystone 的输出逐字节一致。

基准值由 x86 上的真 keystone-engine 0.9.2 生成，覆盖三种关键编码：
  - movw/movt 立即数拼接
  - svc 系统调用
  - ldr rX, =label 的绝对符号重定位（依赖 ld.lld -Ttext=<addr>）
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import keystone

CASES = [
    ("movw r0, #0x1234\nmovt r0, #0x5678\nbx lr\n", 0x10000,
     "340201e3780645e31eff2fe1"),
    ("mov r0, #1\nsvc #0\nbx lr\n", 0x10000,
     "0100a0e3000000ef1eff2fe1"),
    ("ldr r1, =out\nbx lr\nout:\n.word 0x11223344\n", 0x10000,
     "04109fe51eff2fe14433221108000100"),
]


def main():
    ks = keystone.Ks(keystone.KS_ARCH_ARM, keystone.KS_MODE_ARM)
    ok = True
    for i, (src, addr, expected) in enumerate(CASES):
        try:
            code, _ = ks.asm(src, addr)
            got = bytes(code).hex()
        except Exception as exc:
            print("case %d ERROR %s" % (i, exc))
            ok = False
            continue
        if got == expected:
            print("case %d OK" % i)
        else:
            print("case %d MISMATCH" % i)
            print("  exp", expected)
            print("  got", got)
            ok = False
    print("ALL_OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
