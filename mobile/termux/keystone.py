"""keystone-engine 的替身：把 ARM 汇编转发给 Termux 自带的 LLVM 工具链。

A7R3 部署流程用 keystone 在运行时汇编少量 ARM32 机器码（地址依赖
libc_base / mmap 返回值，无法预生成）。Termux 是 aarch64，PyPI 没有对应
的 keystone 轮子，因此这里用 llvm-mc + ld.lld + llvm-objcopy 实现同一接口。

    Ks(KS_ARCH_ARM, KS_MODE_ARM).asm(src, addr) -> (bytes_list, instr_count)

- ld.lld -Ttext=<addr> 负责把 `ldr rX, =label` 这类绝对符号重定位到正确地址；
- llvm-objcopy -O binary --only-section=.text 取出纯机器码。

依赖：pkg install llvm lld
"""
import os
import shutil
import subprocess
import tempfile

KS_ARCH_ARM = 1
KS_MODE_ARM = 0
KS_ARCH_ARM64 = 2


class KsError(Exception):
    pass


def _tool(name):
    path = shutil.which(name)
    if not path:
        raise KsError('missing tool: %s (pkg install llvm lld)' % name)
    return path


class Ks:
    """与 keystone.Ks 兼容的最小实现（仅 ARM 模式）。"""

    def __init__(self, arch, mode):
        self.arch = arch
        self.mode = mode

    @staticmethod
    def _run(cmd):
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            raise KsError('cmd failed: %s\n%s%s' % (' '.join(cmd), p.stdout, p.stderr))

    def asm(self, string, addr=0, count=0):
        if self.mode != KS_MODE_ARM:
            raise KsError('only ARM (32-bit) mode is supported')
        workdir = tempfile.mkdtemp(prefix='a7r3_ks_')
        try:
            src = os.path.join(workdir, 'a.s')
            obj = os.path.join(workdir, 'a.o')
            elf = os.path.join(workdir, 'a.elf')
            raw = os.path.join(workdir, 'a.bin')
            with open(src, 'w', encoding='ascii') as f:
                f.write('.syntax unified\n.arm\n.text\n')
                f.write(string)
                if not string.endswith('\n'):
                    f.write('\n')
            self._run([_tool('llvm-mc'), '-triple=armv7a-none-eabi',
                       '-filetype=obj', '-o', obj, src])
            self._run([_tool('ld.lld'), '-Ttext=0x%x' % addr, '--entry=0',
                       '-o', elf, obj])
            self._run([_tool('llvm-objcopy'), '-O', 'binary',
                       '--only-section=.text', elf, raw])
            with open(raw, 'rb') as f:
                code = f.read()
            return list(code), len(code) // 4
        finally:
            shutil.rmtree(workdir, ignore_errors=True)
