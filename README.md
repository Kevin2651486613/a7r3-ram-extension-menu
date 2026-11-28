# Sony α7R III (ILCE-7RM3 v3.10) 原生拓展菜单（Pure-RAM）

[![Target](https://img.shields.io/badge/Target-ILCE--7RM3%20v3.10-blue.svg)]()
[![Release](https://img.shields.io/github/v/release/Kevin2651486613/a7r3-ram-extension-menu?label=Release)](https://github.com/Kevin2651486613/a7r3-ram-extension-menu/releases/latest)
[![Mode](https://img.shields.io/badge/Mode-Pure--RAM%20(%2Ftmp__bt)-brightgreen.svg)]()
[![Windows](https://img.shields.io/badge/Windows-One--Click%20Auto%20Driver-0078D6.svg)]()
[![Compliance](https://img.shields.io/badge/Firmware-Zero%20Proprietary%20Assets-blue.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)]()

面向 **Sony α7R III（ILCE-7RM3，目标固件 v3.10）** 的纯内存（Pure-RAM）动态拓展框架与自动化工具链。通过 USB Service Mode 将纯原创自研 `.so` 注入运行中的 `im.elf` 进程内存，在相机原厂 `MENU` 顶栏原生扩展出**第 7 大类「拓展菜单」**；全程**零 Flash / NAND 写入、零 `.text` 代码段补丁**，相机断电重启后 100% 恢复原厂状态。

> [!CAUTION]
> **相机变砖与硬件风险免责声明（使用前必读）**：本项目通过 USB Service Mode 与运行中的相机进程直接交互。虽然架构设计严格遵循纯内存、零 Flash 写入，但**操作失误、异常断电、固件版本不匹配（非 v3.10）或未预期的内存状态仍可能导致相机永久变砖、硬件损坏、拍摄数据丢失或官方保修失效**。所有风险由使用者完全自担，作者与贡献者不承担任何责任。使用或二次开发前请完整阅读 **[DISCLAIMER.md](DISCLAIMER.md)**，并仅在 **ILCE-7RM3 固件 v3.10** 的非重要拍摄场景下测试。

> [!NOTE]
> **维护主线与零固件合规声明**：
> - **唯一持续维护主线（`main` / `feat/native-tab7-a1`）**：原生第 7 大类拓展菜单 + 纯内存菜单触摸控制，直接调用原厂 GPU 光栅化与索尼系统字库。
> - **MD3 归档路线（Tag `v1.0-md3` / [`src/legacy/`](src/legacy/)）**：**已暂停开发，仅归档保留**，不再维护，也不再接受 MD3 新功能 PR。
> - **零固件分发**：本仓库**不包含**任何 Sony 固件、专有 `.so`、布局二进制或字库数据；Windows 一键驱动助手 `wdi-simple.exe` 依据上游 LGPL-3.0 / GPL-3.0 协议独立存放在 [`tools/win_driver/`](tools/win_driver/)。

---

## 预编译内置（普通用户免装 LLVM / 免手动 Zadig / 免单独下载 `.so`）

> [!IMPORTANT]
> **普通体验用户不需要安装 LLVM，不需要手动配置 Zadig，也不需要单独下载或联网拉取 `.so`！**
> 纯原创预编译产物已直接内置在 [`prebuilt/demo_native_tab7_ram.so`](prebuilt/demo_native_tab7_ram.so)（随仓库打包）。只要电脑装有 **Python 3.10+**，下载本仓库后**直接双击 [一键注入拓展菜单.bat](一键注入拓展菜单.bat)** 即可全自动完成注入与驱动还原。

| 项目 | 内容 |
| --- | --- |
| 当前版本 | [`v2.2.2-native-tab7`](https://github.com/Kevin2651486613/a7r3-ram-extension-menu/releases/tag/v2.2.2-native-tab7)（[全部 Releases](https://github.com/Kevin2651486613/a7r3-ram-extension-menu/releases/latest)） |
| 随仓库内置路径 | [`prebuilt/demo_native_tab7_ram.so`](prebuilt/demo_native_tab7_ram.so)（`117,592 bytes`，纯原创零 Sony 固件字节，开箱即用） |
| SHA-256 校验 | `6a939a9b8878202cdc50f106991c361082fc922111a671c0009a71a11a4b7482` |
| 独立 Release 镜像 | [`demo_native_tab7_ram.so`](https://github.com/Kevin2651486613/a7r3-ram-extension-menu/releases/latest/download/demo_native_tab7_ram.so)（仅当删空 `prebuilt/` 且无本地 `build/` 时兜底拉取） |

---

## 机内功能与菜单一览（2 页共 10 项）

注入成功后，在相机按下 `MENU` 键，顶栏右侧会出现原厂风格的**第 7 个大类「拓展菜单」**（共 2 页、10 个交互项）：

| 页码 | 菜单项 | 类型 | 功能说明 |
| --- | --- | --- | --- |
| 第 1 页 | **屏蔽曝光补偿波轮** | 开关 | 拦截物理 EV 曝光补偿拨盘事件与轮询状态，防止误触改变曝光 |
| 第 1 页 | **水平仪与直方图同显** | 开关 | 在电子水平仪模式下同步叠显实时直方图（依赖运行时校验载荷，无载荷时自动置灰并安全透传） |
| 第 1 页 | **相册显示焦点位置** | 开关 | 回放照片时自动解析 EXIF Sony MakerNote (`0x2027`) 并叠绘像素级无损对焦红框；兼容连拍组、`DISP`、放大与 `MENU` 退让，支持三缓冲采样并在删除等原生弹窗期间自动抑制防遮挡 |
| 第 1 页 | **菜单触摸控制** | 开关 | 为原生按键式菜单注入纯内存触摸控制：支持 7 大类点击直达、标题栏翻页、行项点击、二级滚动弹窗及白平衡左侧滚轮点选 |
| 第 1 页 | **快门自定义音效** | 四模式选择 | 控制自定义快门音触发时机（`关闭` / `机械快门启用` / `电子快门启用` / `均启用`），通过机身扬声器 ADF 通道异步播放 |
| 第 1 页 | **快门音效音量** | 三档选择 | 调节自定义快门音播放音量（`低` / `中` / `高`） |
| 第 2 页 | **选择快门音效** | 列表选择 | 选择唯一内置测试音效（`内置测试音效`）或 SD 卡 `/SHUTTER_SFX/` 目录下扫描到的 `.wav` 音效 |
| 第 2 页 | **试听当前快门音效** | 执行 | 立即通过机身扬声器试听当前选中的快门音效 |
| 第 2 页 | **刷新SD卡音效列表** | 执行 + 弹窗 | 重新扫描卡槽 1 / 卡槽 2（含 exFAT / FAT32）`/SHUTTER_SFX/` 目录下的 `.wav` 文件，并弹出扫描结果提示框 |
| 第 2 页 | **恢复全部默认设置** | 执行 | 一键将拓展菜单内所有增强开关与音效选项恢复为默认关闭状态 |

### 模块与源码对应关系

- 入口与线程管理：[`src/entries/demo_native_tab7.c`](src/entries/demo_native_tab7.c)
- 原生第 7 大类与设置节点树：[`src/modules/a7r3_native_tab7.c`](src/modules/a7r3_native_tab7.c)
- 纯内存菜单触摸控制：[`src/modules/a7r3_menu_touch.c`](src/modules/a7r3_menu_touch.c)
- 水平仪与直方图同显：[`src/modules/a7r3_dual_hud.c`](src/modules/a7r3_dual_hud.c) —— 运行时从 `/tmp_bt/dual_hud_payload.bin` 读取载荷，加载前执行 `DHUD` 魔数、精确长度 `2380` 与 FNV-1a-32 校验，失败即降级透传；**编译出的 `.so` 本身含零字节 Sony 布局数据**
- 回放对焦点显示：[`src/modules/a7r3_pb_focus.c`](src/modules/a7r3_pb_focus.c)
- 电子快门自定义音效：[`src/modules/a7r3_shutter_sfx.c`](src/modules/a7r3_shutter_sfx.c)
- 底层系统调用、显存采样、输入钩子：[`src/core/`](src/core/)

---

## 快速开始

### 方式一：普通体验用户（Windows 全自动一键注入 — 免装 LLVM、免手动 Zadig，已真机验证）

> [!TIP]
> 仓库已随包内置最小 MIT `pmca` 通信层（[`tools/vendor/pmca/`](tools/vendor/pmca)）与纯原创预编译插件（[`prebuilt/demo_native_tab7_ram.so`](prebuilt/demo_native_tab7_ram.so)），且运行时双显载荷 `/tmp_bt/dual_hud_payload.bin` 与插件完全解耦。普通体验用户**无需安装 LLVM 编译器**、**无需克隆外部仓库**、**无需手动跑 Zadig**、**无需单独下载 `.so`**，双击一键脚本即可完成全部流程。

1. **安装 Python 3.10+（仅首次需要）**：
   - **Windows 一键命令行安装**（Win10 / Win11 打开 PowerShell 运行，装完后重新打开终端或直接双击 `.bat` 刷新环境变量）：
     ```powershell
     winget install Python.Python.3.12
     ```
   - **官方安装包下载**：[Python for Windows 官方下载页](https://www.python.org/downloads/windows/)（其他系统见 [python.org/downloads](https://www.python.org/downloads/)；Windows 安装界面底部务必勾选 **`Add python.exe to PATH`**）。
   - *（Python 依赖会由下一步的双击脚本自动补装，如需提前手动安装也可运行 `pip install -r requirements.txt`。）*
2. **设置相机并连线**：
   - 将相机 `MENU -> 设置(黄箱子) -> USB连接` 设为 **大容量存储 (Mass Storage)**，通过数据线直连电脑并开机。
3. **双击一键注入（全自动闭环）**：
   - 直接双击根目录 **[一键注入拓展菜单.bat](一键注入拓展菜单.bat)**（或以管理员运行 `python tools/a7r3_addon_console.py one-click`）。
   - 脚本会自动完成全部闭环并已在真实 Windows 环境验证通过：
     1. 自动请求 UAC 管理员权限，并检测补装缺失的 3 个 Python 依赖（`keystone-engine`、`pyusb`、`pycryptodomex`）；
     2. 自动校验并选用仓库内置 [`prebuilt/demo_native_tab7_ram.so`](prebuilt/demo_native_tab7_ram.so)（本地有 `build/demo_native_tab7_ram.so` 时优先使用本地编译版）；
     3. 自动将内置驱动助手复制到临时目录（规避下载目录 / 工作区的 Low Mandatory Level 与 Mark-of-the-Web 限制），临时为 `054C:0BFF` 安装 `libusb-win32`；
     4. 自动切入 Service Mode 并临时绑定 `054C:0336` / `02A9`；
     5. 如本机尚无运行时双显载荷，自动从已连接相机只读提取 `viewStlrec.uxc`、校验 SHA-256 后生成并上传 `/tmp_bt/dual_hud_payload.bin`；
     6. 纯 RAM 注入 `demo_native_tab7_ram.so` 到 `im.elf` 并校验 10 项原生钩子全部 `[OK]`；
     7. **结束后自动卸载全部临时 `libusb-win32` 驱动包并通过 `pnputil` 扫描还原 Windows 原生 `USBSTOR`**。
   - 看到注入完成且原生驱动已恢复后即可安全拔线，在相机上按 `MENU` 体验第 7 大类「拓展菜单」。

---

### 方式二：开发者 / 从源码编译（Windows / macOS / Linux）

1. **准备 Python 环境**：同方式一第 1 步（无需单独克隆 `Sony-PMCA-RE`）。
2. **安装 LLVM 交叉编译工具链**（需包含 `clang`、`ld.lld`、`llvm-readelf`，版本 ≥ 16）：
   - **Windows**：一行命令安装，构建脚本会自动探测 `C:\Program Files\LLVM\bin`（无需再手动配置 `.env.local`）：
     ```powershell
     winget install LLVM.LLVM
     ```
   - **macOS**：`brew install llvm libusb`（注意勿使用缺 `ld.lld` 的 Xcode 自带 `/usr/bin/clang`；macOS 仅完成工具链适配，作者尚未在真机 Mac 上实测 USB 注入）。
   - **Linux (Ubuntu/Debian)**：`sudo apt install clang lld llvm libusb-1.0-0`（需配置 Sony `054c` `udev` 规则或用 `sudo` 运行；Linux 同样未在真机外围完成批量用户回归）。
3. **编译并部署**：
   - **Windows 一键重新编译 + 注入 + 还原驱动**：
     ```powershell
     .\一键注入拓展菜单.bat --rebuild
     ```
     *(若本地尚无 `firmware/lib/`，脚本会自动从已连接相机只读提取 `libc-2.11.2.so`、`libdl-2.11.2.so`、`libpthread-2.11.2.so` 到被 `.gitignore` 忽略的 `firmware/lib/` 目录。)*
   - **常用 CLI 子命令**（可运行 `python tools/a7r3_addon_console.py <命令>` 或双击 **[启动拓展菜单工具.bat](启动拓展菜单工具.bat)** 打开交互菜单）：

     | 命令 | 作用 |
     | --- | --- |
     | `one-click` | 缺包自动 `pip` → 优先用 `build/` 或内置 `prebuilt/.so` → 自动换驱 → 注入 → 全量还原驱动 |
     | `one-click --rebuild` | 强制从源码重新编译 `.so` 并完成全自动注入与驱动还原 |
     | `download-so` | 手动从 GitHub Releases 拉取最新版预编译 `demo_native_tab7_ram.so` 并校验 SHA-256 |
     | `init-camera` | 连机从相机只读拉取 `/lib` 三件套（MD5 校验）与运行时双显载荷 `.bin` |
     | `check` | 离线检查 Python 依赖、工具链、驱动助手状态、运行时载荷与 `.so` ELF 边界 |
     | `build` | 单独交叉编译生成 `build/demo_native_tab7_ram.so` |
     | `deploy` | 将当前 `build/demo_native_tab7_ram.so`（及双显载荷）部署进相机内存 |
     | `update` | 编译 + 部署（不自动换驱，适合已手动配置好 libusb 的开发者） |
     | `status` | 查询在线 `im.elf` PID、`/tmp_bt/addon.log` 挂钩摘要与最近日志 |
     | `quit` | 不重启相机安全卸载全部钩子并退出插件线程 |
     | `restore-driver` | Windows 手动卸载全部 Sony 临时 `libusb-win32` 驱动包并恢复 `USBSTOR` |
     | `gen-payload --from-camera` | 单独从已连接相机或本地固件生成 `build/dual_hud_payload.bin` |

---

## 安全边界与 Fail-Closed 设计

1. **零 Flash / NAND 写入**：所有部署文件和运行时日志严格限制在 `/tmp_bt/`；部署前强制验证 `/tmp_bt` 为 `tmpfs` 挂载点且剩余空间充足，断电重启即彻底清空。
2. **零 `.text` 代码段补丁**：绝不改写固件只读代码页。全部功能钩子均通过原子替换 `.got` 表槽位，或复制可写 C++ 虚表并切换实例 `vptr` 实现。
3. **多重 Fail-Closed 校验**：
   - [`deploy_ram.py`](tools/deploy_ram.py)：严格校验 `ELF32 ARM ET_DYN` 格式、`/tmp_bt/` 白名单路径、`pidof im.elf` 唯一性、核心动态库映射、`31` 槽位上限与远程 `mmap2` 地址范围；相机型号校验非 `ILCE-7RM3` 或无法读取时直接中止（不默认绕过）。
   - [`gen_dual_hud_payload.py`](tools/gen_dual_hud_payload.py) & [`a7r3_dual_hud.c`](src/modules/a7r3_dual_hud.c)：构建载荷时强制对源 `viewStlrec.uxc` 记录执行 SHA-256 指纹校验；机内读取 `/tmp_bt/dual_hud_payload.bin` 时再次校验 `"DHUD"` 魔数、`2380` 长度与 FNV-1a-32 校验和。
   - **禁止 `dlclose()` 强卸**：卸载流程先还原 `.got` / `vptr` 并通知工作线程安全退出，避免仍在函数内的系统线程退栈崩溃。
4. **Windows 自动驱动回滚**：即使安装或注入阶段中断，再次运行 `一键注入拓展菜单.bat` 或 `python tools/a7r3_addon_console.py restore-driver` 均可清除全部 Sony 临时 `libusb-win32` INF 包并还原原生 `USBSTOR`。

---

## SD 卡自定义快门音效（可选）

在相机存储卡根目录创建 `/SHUTTER_SFX/` 文件夹，放入**未压缩 PCM `.wav`** 音频（推荐 `24 kHz`、单声道、`16-bit PCM`，时长建议 `<= 0.65 s`），即可在菜单第 2 页选择并试听：

```powershell
ffmpeg -i input.mp3 -c:a pcm_s16le -ar 24000 -ac 1 shutter_custom.wav
```

---

## 仓库目录结构

```text
a7r3-ram-extension-menu/
├── README.md                          # 项目主页与使用指南（本文件）
├── DISCLAIMER.md                      # 相机变砖免责声明、稳定性警告与合规声明（必读）
├── CONTRIBUTING.md                    # 二次开发、Issue 与 PR 规范（含安全红线）
├── CREDITS.md                         # 引用工程、外部依赖与协议完整清单
├── LICENSE                            # MIT 开源协议与第三方随附工具说明
├── requirements.txt                   # Python 依赖清单
├── .env.example                       # 可选本机路径覆盖模板（按需复制为 .env.local）
├── .gitignore                         # 忽略 build/、.env.local、*.local.h、firmware/、experiments/ 等
├── .github/                           # Issue 表单模板与 PR 模板
├── 一键注入拓展菜单.bat                # Windows 一键全自动闭环入口（自动 UAC / 换驱 / 注入 / 还原，CRLF）
├── 启动拓展菜单工具.bat                # Windows 交互式控制台入口（CRLF）
├── prebuilt/                          # 随仓库内置纯原创预编译 .so（开箱即用，零专有固件字节）
├── src/
│   ├── core/                          # 核心 SDK：系统调用、显存读取、绘制、输入与按键钩子
│   ├── modules/                       # 功能模块：第 7 大类、菜单触摸、双显、回放对焦、快门音效
│   ├── entries/                       # 主线入口：demo_native_tab7.c
│   └── legacy/                        # 已暂停开发的 MD3 / 早期示例归档（默认不参与编译）
├── tools/
│   ├── env_config.py                  # 加载可选 .env.local 环境变量
│   ├── a7r3_addon_console.py          # 统一交互式 / CLI 控制台
│   ├── win_usb_driver.py              # Windows 临时 libusb-win32 驱动切换与全量还原管理器
│   ├── win_driver/                    # 独立随附命令行助手 wdi-simple.exe（pbatard/libwdi v1.5.1, LGPL-3.0）
│   ├── build_addon.py                 # freestanding 编译与 ELF 严格校验
│   ├── deploy_ram.py                  # 多重边界校验纯 RAM (/tmp_bt) 部署器
│   ├── gen_dual_hud_payload.py        # 提取并校验 viewStlrec.uxc 生成运行时 dual_hud_payload.bin
│   ├── gen_md3_assets.py              # （归档）仅 legacy MD3 使用
│   ├── vendor/pmca/                   # 内置精简版 USB/Senser 通信层（ma1co/Sony-PMCA-RE, MIT）
│   └── ram_loader/                    # 纯 RAM 注入助手（MIT 清洁室内置实现）
└── docs/
    ├── ARCHITECTURE.md                # 总体架构、纯 RAM 注入与各模块实现思路
    ├── FIRMWARE_OFFSETS_V310.md       # A7R3 v3.10 互操作性地址偏移与通信协议笔记
    ├── TOOLS_AND_BUILD.md             # 工具链配置、编译约束、控制台命令与音频转码
    ├── LESSONS_LEARNED.md             # 实机调试踩坑记录与根因分析
    └── AGENT_PROMPT.md                # 给 AI Agent 的环境准备与离线自检提示词
```

---

## 常见问题与排查

| 现象 / 提示 | 根因 | 解决方法 |
| --- | --- | --- |
| `未找到可用插件` 且无 LLVM | 删空了 `prebuilt/` 且未自行编译 | 恢复 [`prebuilt/demo_native_tab7_ram.so`](prebuilt/demo_native_tab7_ram.so)，或开发者安装 LLVM 后加 `--rebuild` |
| `未找到 A7R3 v3.10 系统库` | 触发源码编译但本机尚无 `firmware/lib/` | 连上相机运行 `one-click --rebuild` 或 `init-camera` 自动只读提取（使用预编译 `.so` 无需此步） |
| 一直提示处于 **MTP 模式** | 相机 USB 模式设为了 MTP / PC 遥控 | 相机 `MENU -> 设置 -> USB连接` 改为 **大容量存储 (Mass Storage)** |
| **注入中途拔线**导致电脑打不开存储卡 | 临时 `libusb-win32` 驱动尚未卸载 | 运行 `python tools/a7r3_addon_console.py restore-driver`（或在控制台选 `[8]`）自动还原为原生 `USBSTOR` |
| `pyusb` / `pycryptodomex` 缺失 | 尚未安装精简 Python 依赖 | 在仓库根目录执行 `pip install -r requirements.txt`（`pmca` 已内置，无需再克隆 `Sony-PMCA-RE`） |
| 菜单里「水平仪与直方图同显」**灰色不可选** | `/tmp_bt/dual_hud_payload.bin` 缺失或指纹校验失败（仅支持 v3.10） | 确认相机固件为 **ILCE-7RM3 v3.10**，重新运行一次 `one-click` / `deploy` 或 `gen-payload --from-camera` |
| 不关机直接恢复原厂菜单 | 需在线卸载 RAM 模块钩子 | 连线运行 `python tools/a7r3_addon_console.py quit`，或直接关闭相机开关重开 |

---

## 引用工程与致谢 (Credits & Acknowledgements)

本项目的实现建立在诸多开源工具与 Sony 相机逆向社区先行研究之上，完整依赖、作者与许可清单请见 **[CREDITS.md](CREDITS.md)**：

- **[ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)**（`ma1co`，MIT）：USB Service Mode 握手、Senser 终端与文件读写底层通道；
- **[pbatard/libwdi](https://github.com/pbatard/libwdi)**（LGPL-3.0）& **[mcuee/libusb-win32](https://github.com/mcuee/libusb-win32)**（GPL-3.0 / LGPL-3.0）：提供 Windows 一键自动换驱独立助手 `wdi-simple.exe`；
- **[Bigrabbit057/sony-a7r3-creative-studio](https://github.com/Bigrabbit057/sony-a7r3-creative-studio)**（`Bigrabbit057`）：A7R3 双显 HUD 与显存结构研究参考（本项目均采用 MIT 清洁室重写，未复用其代码）；
- **[Derek2011cc](https://github.com/Derek2011cc/A7M3-Firmware-QEMU-Boot-Experiment)**（*A7M3-Firmware-QEMU-Boot-Experiment* / *a7m3-QEMU-shell* / *a7m3-connect-shell*）与 **[chocotan](https://github.com/chocotan/a7c2-qemu)**（*a7c2-qemu* / *a7m3-qemu* / *zve10-qemu*）：QEMU 固件仿真、OSD 渲染与纯内存注入方法论参考；
- **[robinshie/Sony_A7M3](https://github.com/robinshie/Sony_A7M3)**、**DavidBuchanan314**（*ILCE-7M4-RE*）、**Lost-week-1025**（*CXD90045-FPSU*）、**YuuLuo**（*a7c2-service-preload*）：Sony Alpha 分区、符号与服务模式先行研究；
- **[LLVM / Clang + LLD](https://github.com/llvm/llvm-project)** 与 **[Keystone Engine](https://github.com/keystone-engine/keystone)**：ARMv7-A 交叉编译与远程跳板汇编工具链。

---

## 文档导航与参与贡献

- **[相机变砖免责与合规声明 (DISCLAIMER.md)](DISCLAIMER.md)**
- **[二次开发、Issue 与 PR 规范 (CONTRIBUTING.md)](CONTRIBUTING.md)**
- **[引用工程、外部依赖与致谢 (CREDITS.md)](CREDITS.md)**
- [总体架构与各模块实现思路 (docs/ARCHITECTURE.md)](docs/ARCHITECTURE.md)
- [A7R3 v3.10 逆向偏移与协议笔记 (docs/FIRMWARE_OFFSETS_V310.md)](docs/FIRMWARE_OFFSETS_V310.md)
- [工具链配置、编译约束与部署指南 (docs/TOOLS_AND_BUILD.md)](docs/TOOLS_AND_BUILD.md)
- [实机调试踩坑记录与根因分析 (docs/LESSONS_LEARNED.md)](docs/LESSONS_LEARNED.md)
- [给 AI Agent 的环境准备与离线自检提示词 (docs/AGENT_PROMPT.md)](docs/AGENT_PROMPT.md)

---

## 支持项目

如果你喜欢本项目并觉得它对你有帮助，欢迎扫码支持（支付宝 / 微信支付）。

收到的资金将全部用于该项目的持续推进或相关项目的开发，希望大家多多支持，减轻一些 token 压力。

![支持项目收款码](docs/images/support-qr.png)
