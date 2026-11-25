# 致谢与外部依赖说明 / CREDITS

本仓库的原创 C SDK、MD3 UI 引擎、纯内存挂钩模块与技术文档均以 **MIT 协议**开源，且仓库中**不包含**任何 Sony 原厂固件二进制文件、专有字库或第三方非兼容协议源码。

以下列出开发、构建与实机运行阶段使用/参考的外部工具与开源研究：

## 1. 构建与运行时外部依赖（需自行准备，未随本仓库分发）

| 组件 | 协议 | 用途 |
| --- | --- | --- |
| [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) (`pmca` / `libusb`) | MIT | USB Service Mode 握手、终端认证与 `/tmp_bt` 文件读写通道 |
| [LLVM / Clang + LLD](https://llvm.org/) (`clang` / `ld.lld` / `llvm-readelf`) | Apache-2.0 with LLVM-exception | 交叉编译 ARMv7-A freestanding `.so` 与远程注入 ELF |
| [Keystone Engine](https://www.keystone-engine.org/) (`keystone-engine`) | GPLv2 | 在宿主机端汇编远程 ARM ELF 工具（`tools/ram_loader/`，构建产物与注入流程独立于上游工具许可） |
| [Pillow](https://python-pillow.org/) | HPND | `tools/gen_md3_assets.py` 光栅化生成 4-bit 抗锯齿中文字模头文件 |
| [FFmpeg](https://ffmpeg.org/) | LGPL / GPL | 将外部音频批量转换为机内支持的 24 kHz 单声道 16-bit PCM `.wav` |

> 注：纯 RAM 注入所需的 `tools/ram_loader/terminal_shell.py`、`build_mem_writer.py`、`build_show_hex.py` 已采用清洁室方式基于 MIT 协议从零重写并直接内置于本仓库（仅调用 MIT 许可的 `Sony-PMCA-RE` 终端收发接口与 Linux 标准系统调用）。

## 2. 公开逆向研究参考

- **ma1co** — *Sony-PMCA-RE*、固件解包与服务模式通信协议研究
- **Bigrabbit057** — *sony-a7r3-creative-studio*（A7R3 双显 HUD、显存与固件逆向）
- **DavidBuchanan314** — *ILCE-7M4-RE*、*Sony-ILCE-7M4-Linux*
- **robinshie** — *Sony_A7M3*（分区/符号参考）
- **chocotan** / **Derek2011cc** — Sony 相机 QEMU、OSD 绘制与内存注入方法论参考

## 3. 商标与免责声明

- Sony、ILCE-7RM3、α7R III 等商标归 Sony Group Corporation 所有，本项目与其无任何官方关联。
- 所有功能均以“纯 RAM 热注入”方式运行于易失性内存，断电重启即恢复原厂状态；使用任何未公开调试接口仍存在潜在风险，请用户自行评估并承担责任。
