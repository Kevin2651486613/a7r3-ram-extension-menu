# 引用工程与致谢 / CREDITS & ACKNOWLEDGEMENTS

本仓库的原创 C SDK、纯内存挂钩模块、自动化工具链与技术文档均以 **[MIT 协议](LICENSE)** 开源；仓库中**不包含**任何 Sony 原厂固件二进制文件、专有字库，也未在主线源码中混入非兼容商用限制协议的代码。

以下完整列出本项目使用、集成或参考的外部工程、工具链与公开逆向研究。

---

## 1. 随仓库附带的第三方组件

| 组件 | 上游项目 / 作者 | 协议 | 说明 |
| --- | --- | --- | --- |
| `tools/vendor/pmca/` | [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) (`ma1co`) | **MIT** | 按最小闭包精简内置的 USB Service Mode 握手、Senser 终端与 `/tmp_bt` 文件读写子集（去除 APK/商店/固件/TLS 等无关模块）；详见 [tools/vendor/pmca/README.md](tools/vendor/pmca/README.md) 与 [LICENSE-MIT.txt](tools/vendor/pmca/LICENSE-MIT.txt) |
| `tools/win_driver/wdi-simple.exe` | [pbatard/libwdi](https://github.com/pbatard/libwdi) (`v1.5.1`) & [mcuee/libusb-win32](https://github.com/mcuee/libusb-win32) (`v1.4.0.0`) | **LGPL-3.0** / **GPL-3.0** | Windows 端独立子进程命令行工具，用于为相连 Sony 相机临时安装 `libusb0` 并在注入完成后通过 `pnputil` 卸载还原；详见 [tools/win_driver/README.md](tools/win_driver/README.md) 与 [LICENSE-LGPL-3.0.txt](tools/win_driver/LICENSE-LGPL-3.0.txt) |

---

## 2. 构建与运行时外部依赖（`pip` / 系统工具）

| 组件 | 上游项目 / 组织 | 协议 | 用途 |
| --- | --- | --- | --- |
| **LLVM / Clang + LLD** | [llvm/llvm-project](https://github.com/llvm/llvm-project) | Apache-2.0 with LLVM-exception | 交叉编译 ARMv7-A freestanding `.so` 与 ELF 校验（`clang` / `ld.lld` / `llvm-readelf`；使用预编译 Release `.so` 时不需要） |
| **Keystone Engine** | [keystone-engine/keystone](https://github.com/keystone-engine/keystone) | GPLv2 | 宿主机端汇编远程 ARM ELF 工具（`tools/ram_loader/` 自身为 MIT 清洁室实现） |
| **PyUSB / libusb** | [pyusb/pyusb](https://github.com/pyusb/pyusb) & [libusb/libusb](https://github.com/libusb/libusb) | BSD-3-Clause / LGPL-2.1 | 跨平台 USB 底层读写接口 |
| **Termux / Termux:API** | [termux/termux-api](https://github.com/termux/termux-api) & [termux/termux-packages](https://github.com/termux/termux-packages) | **GPL-3.0**（termux-api）；其余各包按上游协议 | `mobile/` 手机方案在 Android 上的运行环境，提供 `termux-usb` 文件描述符桥接；不随仓库分发，由用户从 Termux 软件源安装 |
| **python-libusb1** | [vpelletier/python-libusb1](https://github.com/vpelletier/python-libusb1) | **LGPL-2.1** | `mobile/` 手机方案中把 `termux-usb` 传入的 fd 包装为 libusb 句柄（`libusb_wrap_sys_device`）；不随仓库分发 |
| **PyCryptodomex** | [Legrandin/pycryptodome](https://github.com/Legrandin/pycryptodome) | BSD-2-Clause / Public Domain | Senser 认证握手所需的 SHA-256 哈希计算 |
| **FFmpeg**（可选） | [FFmpeg](https://ffmpeg.org/) | LGPL / GPL | 将外部音频转换为机内兼容的 24 kHz 单声道 16-bit PCM `.wav` |
| **Pillow**（可选/归档） | [python-pillow/Pillow](https://github.com/python-pillow/Pillow) | HPND | 仅用于手动编译已暂停的 `src/legacy/` MD3 归档路线生成点阵字模（Tab7 主线不需要） |

> 注：纯 RAM 注入所需的 [terminal_shell.py](tools/ram_loader/terminal_shell.py)、[build_mem_writer.py](tools/ram_loader/build_mem_writer.py)、[build_show_hex.py](tools/ram_loader/build_show_hex.py) 与 [build_ram_tools.py](tools/ram_loader/build_ram_tools.py) 均采用清洁室方式基于 MIT 协议从零实现，仅调用内置精简版 `tools/vendor/pmca/` 的终端收发接口与 Linux 标准系统调用。

> 注：`mobile/` 手机方案**不依赖 Keystone** —— aarch64 的 Termux 暂无可用轮子，[mobile/termux/keystone.py](mobile/termux/keystone.py) 把同一调用接口转发给 Termux 软件源的 `llvm-mc` / `ld.lld` / `llvm-objcopy`，因此复用上表中的 LLVM 依赖即可，且输出与真 keystone 逐字节一致（[mobile/termux/selftest.py](mobile/termux/selftest.py) 可验证）。

---

## 3. 公开逆向研究与引用工程致谢

本项目的实现思路、固件互操作偏移方法论及纯内存调试经验参考了以下优秀的开源项目与社区研究（均以**思路参考与清洁室互操作研究**方式借鉴，未直接拷贝其代码或专有资产）：

| 项目 / 研究 | 作者 / 维护者 | 参考方向与致谢内容 |
| --- | --- | --- |
| [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) | ma1co | Sony USB Service Mode / Senser 通信协议、固件解包与 `fwtool.py` 先行研究 |
| [Bigrabbit057/sony-a7r3-creative-studio](https://github.com/Bigrabbit057/sony-a7r3-creative-studio) | Bigrabbit057 | A7R3 双显 HUD 布局思路、显存结构、Service Mode 交互与固件研究（上游为 GPLv3；本仓库相关钩子与 `gen_dual_hud_payload.py` 均为 MIT 清洁室原创实现，未复用其代码） |
| [Derek2011cc/A7M3-Firmware-QEMU-Boot-Experiment](https://github.com/Derek2011cc/A7M3-Firmware-QEMU-Boot-Experiment) / [a7m3-QEMU-shell](https://github.com/Derek2011cc/a7m3-QEMU-shell) / [a7m3-connect-shell](https://github.com/Derek2011cc/a7m3-connect-shell) | Derek2011cc | Sony ARM 固件 QEMU 仿真环境、2D 光栅与真机 Shell 连接/内存注入方法论 |
| [chocotan/a7c2-qemu](https://github.com/chocotan/a7c2-qemu) / [a7m3-qemu](https://github.com/chocotan/a7m3-qemu) / [zve10-qemu](https://github.com/chocotan/zve10-qemu) | chocotan | Sony 相机 QEMU 调试、原生菜单设置树与 OSD 渲染探索经验 |
| [robinshie/Sony_A7M3](https://github.com/robinshie/Sony_A7M3) | robin code | Sony 机身分区结构、文件系统与固件符号整理 |
| *ILCE-7M4-RE* / *Sony-ILCE-7M4-Linux* | DavidBuchanan314 | Sony Alpha 平台逆向分析与 Linux/ARM 工具链研究 |
| *CXD90045-FPSU* | Lost-week-1025 | CXD90045 SoC 平台与底层硬件资料参考 |
| *a7c2-service-preload* | YuuLuo | Service Mode 与进程内动态预加载先行探索 |

向以上所有作者与 Sony 开源逆向社区贡献者致以诚挚感谢。

---

## 4. 商标与免责声明

- Sony、α / Alpha、ILCE-7RM3、α7R III 等均为 Sony Group Corporation 的注册商标，本项目与其无任何官方从属或授权关系。
- 使用与风险责任请完整阅读 **[DISCLAIMER.md](DISCLAIMER.md)**。
