# Sony ILCE-7RM3 (A7R3 v3.10) 原生第 7 大类拓展菜单（Route A1 分支）

[![Branch](https://img.shields.io/badge/Branch-feat%2Fnative--tab7--a1%20(Route%20A1)-purple.svg)]()
[![Status](https://img.shields.io/badge/Status-Experimental%20%2F%20R%26D%20Only-orange.svg)]()
[![Compliance](https://img.shields.io/badge/Firmware-Zero%20Proprietary%20Assets-blue.svg)]()
[![Safety](https://img.shields.io/badge/Mode-Pure%20RAM%20(%2Ftmp__bt)-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)]()

> [!NOTE]
> **版本与分支说明**：
> - Tag `v1.0-md3`：早期 Material Design 3 自绘卡片菜单版（**已暂停开发，仅归档保留，不再维护**）；相关源码已移至 `src/legacy/`，默认不参与编译，也不再接受 MD3 新功能 PR。
> - **`main` / `feat/native-tab7-a1`（当前唯一持续维护的 Route A1 主线）**：**原生索尼 UI 第 7 大类 + 纯内存菜单触摸版**——无需任何全屏显存自绘与防闪烁守卫，直接在原厂 `MENU` 顶栏新增第 7 个大类 Tab，使用原厂 GPU 渲染与索尼中文字库、原生二级选择弹窗与拨轮/按键交互，内置第 1 页第 4 项可开关的 **「菜单触摸控制」**；已完整内置 MIT 协议纯内存注入助手与 `requirements.txt`（支持 Windows / macOS / Linux），并修复冷启动后动态堆区扩增导致的 `/proc/self/maps` 缓存过期问题。

> [!WARNING]
> **测试开发与合规声明（必读）**：
> 1. **仅限测试开发**：本工程与分支为 **实验性（Experimental）研究与二次开发框架**，当前仍存在潜在不稳定与边界兼容性风险，**切勿用于商业或重要拍摄**；**欢迎社区基于本框架进行二次开发与改进**。
> 2. **零固件 / 合规仓库**：为遵守开源与版权规范，本仓库**已彻底剔除**所有 Sony 原厂固件二进制、原厂布局数据字节（`a7r3_dual_hud_payload` 已解耦为本地可选文件）及预生成字体位图，Route A1 亦**不包含任何 `.uxc` 修改后的布局二进制**，仅通过纯 C 逻辑动态调用原厂控件，详情请阅读 **[DISCLAIMER.md](DISCLAIMER.md)**。

> [!CAUTION]
> **相机变砖与硬件风险免责声明（必读）**：本项目通过 USB Service Mode 与运行中的相机进程交互。虽然设计目标是纯内存、零 Flash 写入，但**仍可能导致相机永久变砖、硬件损坏、数据丢失与官方保修失效**，所有风险由使用者完全自担，作者与贡献者不承担任何责任。使用或二次开发前请完整阅读 **[DISCLAIMER.md](DISCLAIMER.md)**，并仅在 ILCE-7RM3 v3.10 上、于非重要拍摄场景下测试。

一个面向 **Sony ILCE-7RM3（α7R III，目标固件版本 v3.10）** 的**纯内存（Pure-RAM）动态拓展框架、工具链与逆向思路整理仓库**。

无需刷机、无需改写任何 NAND/Flash 分区，通过 USB Service Terminal 在 `im.elf` 中以 `/tmp_bt` (`tmpfs`) 纯内存方式加载自定义 `.so`；**断电重启后 100% 恢复出厂状态。**

---

## 核心安全与边界原则

1. **零 Flash / NAND 写入**：所有运行时文件与日志仅写入 `/tmp_bt`（且部署脚本强制校验挂载类型为 `tmpfs` 与剩余容量）。
2. **零 `.text` 代码段补丁**：绝不修改只读代码页，全部钩子均通过原子替换 `.got` 槽位或复制 C++ 虚表并切换 `vptr` 实现。
3. **部署前严格边界检查**：
   - [deploy_ram.py](tools/deploy_ram.py) 内置：ELF32 ARM `ET_DYN` 文件头与大小校验、路径正则约束（拒绝非 `/tmp_bt/<合法文件名>` 与 `..`）、`pidof im.elf` + `/proc/<pid>/comm` 唯一性检验、核心模块映射检验、31 槽位耗尽防覆写保护、远程 `mmap2` 地址范围校验。
   - [a7r3_addon_console.py](tools/a7r3_addon_console.py) 内置：Python 版本检查、旧实例卸载确认超时中止（未确认 `SDK_TAB7_QUIT` / `SDK_CERT_QUIT` 绝不二次注入）、支持离线环境自检命令 `check`（菜单 `[7]`）。
4. **禁止 `dlclose()`**：卸载时还原全部原生指针并等待线程自然退栈，避免正在阻塞于钩子中的系统线程崩溃。

---

## 已验证实现的功能与思路

| 功能模块 | 源码 | 核心逆向/实现思路 |
| --- | --- | --- |
| **原生第 7 大类 UI（Route A1 主线）** | [a7r3_native_tab7.c](src/modules/a7r3_native_tab7.c) / [demo_native_tab7.c](src/entries/demo_native_tab7.c) | 扩展 `viewUnified2` 活动设置树（`0x00a51c60`）为 7 个大类（2 页 6+4 共 10 项原厂条目）；后置定位 `{533, 0, 91, 66}` 并修复顶栏底框 `0x020cc146`；`GEN_Icon::setScalingFlag` + `setImageFilter` 将 `105×66` 底色缩放入 `91×66` 槽位，并用运行时 `ResourceManager::getImageSize` 居中对齐前景图标 `0x0205cb79`；托管 `proc_id (0x7f01..0x7f0a)` 与 `TextSprite` UTF-8 解析 |
| **原生菜单纯内存触摸控制** | [a7r3_menu_touch.c](src/modules/a7r3_menu_touch.c) / [a7r3_input.c](src/core/a7r3_input.c) | **零 `.text` 代码补丁、零堆扫描**；在第 7 大类第 1 页第 4 项提供开关；通过可写虚表追踪 `ViewSettingMenu`、`PAS_MenuListTab`、自定义按键列表/功能页及白平衡专用页；支持 7 大类一步直达、标题栏翻页、6 行直达打开、二级弹窗（含滚动列表与框外取消）、不可设定提示全屏点击返回与白平衡左侧滚轮点选 |
| 屏蔽曝光补偿波轮 | [a7r3_input.c](src/core/a7r3_input.c) | 钩住 `osal_rcv_msg_tmo`、`get_phy` / `get_ev` 并拦截 `0x70d2` / `0x70d3`，同时提供 OSAL 队列按键注入与非吞噬式触控释放监听 |
| 水平仪与直方图同显（自动/本地生成载荷） | [a7r3_dual_hud.c](src/modules/a7r3_dual_hud.c) / [gen_dual_hud_payload.py](tools/gen_dual_hud_payload.py) | 钩住 `getResID` / `RecDtor` / `UserGetLayoutMode` / 路由槽；**默认无载荷安全透传并在菜单置灰**，构建时自动从本地 `viewStlrec.uxc`（或运行 `--from-camera` 从已连接相机只读读取）生成受 `.gitignore` 忽略的 `src/a7r3_dual_hud_payload.local.h` 启用 |
| 相册回放显示对焦点 | [a7r3_pb_focus.c](src/modules/a7r3_pb_focus.c) | `FeaCore` + `PlayStillSingle` 捕获 EXIF，机内解析 Sony MakerNote (`0x2027`) 对焦坐标并叠绘像素级无损红框，兼容连拍组/DISP/缩放/MENU 退让 |
| 电子快门自定义快门音效 | [a7r3_shutter_sfx.c](src/modules/a7r3_shutter_sfx.c) | 钩住 `ObjStillRecorder::ParseRecCntInfoPinCommand`；使用线程安全 `Backup_read(0x01070b2c)` 判定静音状态；绕开 8.3 限制调用 `PrFILE_fsfirst`/`pf2_fopen` 扫描 `/SHUTTER_SFX/*.wav`；机内 WAV→Sony 32B BFP 编码 + `futex` + `DataflowInfraCh::sndAsyncDataflowMsg` 异步发声 |

---

## 仓库目录结构

```text
a7r3-ram-extension-menu/
├── README.md                          # 项目说明（本文件）
├── DISCLAIMER.md                      # 免责声明（含相机变砖风险）、稳定性警告与开源合规声明（必读）
├── CONTRIBUTING.md                    # 二次开发、Issue 与 PR 规范（含安全红线）
├── LICENSE                            # MIT 开源协议
├── CREDITS.md                         # 致谢与外部依赖说明
├── requirements.txt                   # Python 构建与 USB 注入依赖清单（跨平台）
├── .env.example                       # 环境变量模板（复制到 .env.local 进行本机配置）
├── .gitignore                         # 忽略 build/、.env.local、*.local.h、firmware/、experiments/ 等
├── .github/                           # Issue 表单模板与 PR 模板
├── 一键注入拓展菜单.bat                # Windows 一键全自动闭环入口（自动提权/换驱动/注入/还原，CRLF）
├── 启动拓展菜单工具.bat                # Windows 交互式控制台启动入口（保持 CRLF）
├── src/
│   ├── core/                          # 核心 SDK：系统调用、显存、绘制、输入钩子
│   ├── modules/                       # 功能模块：第 7 大类、菜单触摸、双显、回放对焦、快门音效
│   ├── entries/                       # 入口文件：demo_native_tab7.c（Tab7 主线）
│   ├── legacy/                        # 已暂停开发的 MD3 / 早期示例归档（默认不参与编译）
│   └── a7r3_dual_hud_payload.local.h.example # 可选本地 Dual-HUD 载荷模板（零字节）
├── tools/
│   ├── env_config.py                  # 加载可选 .env.local 环境变量
│   ├── a7r3_addon_console.py          # 交互式 / CLI 控制台（含 one-click、离线自检与安全部署）
│   ├── win_usb_driver.py              # Windows 临时 libusb-win32 驱动切换与全量还原管理器
│   ├── win_driver/                    # 内置独立 wdi-simple.exe（pbatard/libwdi, LGPL-3.0）
│   ├── build_addon.py                 # freestanding 编译、自动生成字模与 ELF 严格校验
│   ├── deploy_ram.py                  # 带多重边界校验的纯 RAM (/tmp_bt) 注入器
│   ├── gen_dual_hud_payload.py        # 从本机或已连接相机的 viewStlrec.uxc 自动生成本地双显载荷
│   ├── gen_md3_assets.py              # （归档）仅 legacy MD3 使用：生成 4-bit 中文点阵头文件
│   └── ram_loader/                    # 纯 RAM 注入助手（全内置 MIT 实现）
│       ├── build_ram_tools.py         # 远程 SVC 与 ARM dlopen 跳板生成器
│       ├── terminal_shell.py          # USB Service Terminal BusyBox 命令收发器
│       ├── build_mem_writer.py        # /proc/<pid>/mem 与 ptrace 双路径写入器
│       └── build_show_hex.py          # 流式只读内存/文件十六进制读取器
└── docs/
    ├── ARCHITECTURE.md                # 总体架构、纯 RAM 注入及各模块实现思路
    ├── FIRMWARE_OFFSETS_V310.md       # A7R3 v3.10 互操作性地址偏移与通信协议笔记
    ├── TOOLS_AND_BUILD.md             # 工具链配置、编译约束、控制台命令与 WAV 转码
    ├── LESSONS_LEARNED.md             # 实机调试踩坑记录与根因分析
    └── AGENT_PROMPT.md                # 给 AI Agent 的环境准备与离线自检提示词
```

---

## 快速开始（完整闭环指南）

### 1. 仓库外必须自备的 3 项前提（因体积 / 协议 / 系统权限未打包）

1. **Python 依赖与上游 `Sony-PMCA-RE`（MIT 独立仓库）**：
   ```bash
   pip install -r requirements.txt
   git clone https://github.com/ma1co/Sony-PMCA-RE.git ../Sony-PMCA-RE
   ```
2. **LLVM 交叉编译工具链（体量过大未打包，需含 `clang` + `ld.lld` + `llvm-readelf`）**：
   - **Windows**：安装官方 [LLVM Releases](https://github.com/llvm/llvm-project/releases) 预编译包（并在 `.env.local` 配置 `A7R3_LLVM_BIN`，或将其 `bin` 加入 `PATH`）。
   - **macOS**：`brew install llvm libusb`（注意不能只靠 Xcode CLT 自带的 `/usr/bin/clang`，因为它缺少 `ld.lld`；脚本会自动探测 `/opt/homebrew/opt/llvm/bin` 与 `/usr/local/opt/llvm/bin`）。
   - **Linux (Ubuntu/Debian)**：`sudo apt install clang lld llvm libusb-1.0-0`。
3. **操作系统 USB 驱动与权限**：
   - **Windows（已全自动闭环，无需手动下载或点击 Zadig）**：
     - 仓库已内置独立的 `wdi-simple.exe`（来自 `pbatard/libwdi` v1.5.1 / `libusb-win32` v1.4.0.0）与 `tools/win_usb_driver.py`。
     - 将相机设为 **大容量存储 (Mass Storage)** 并连线后，只需双击根目录 **`一键注入拓展菜单.bat`**（或以管理员执行 `python tools/a7r3_addon_console.py one-click`）：脚本会自动通过 UAC 提权，为相机（`054C:0BFF` 及切换后的 Service Mode `054C:0336`）临时绑定 `libusb-win32`，首次缺库时自动只读初始化并编译，纯 RAM 注入并校验 10 项钩子后，**自动卸载全部临时 `libusb-win32` 驱动并把相机恢复为原生 `USBSTOR`**，不留残留驱动。
     - 如因中途拔线等异常残留了临时驱动，可随时执行 `python tools/a7r3_addon_console.py restore-driver`（或在控制台选 `[8]`）一键清除。
   - **Linux**：普通用户需配置 Sony (`054c`) USB `udev` 权限规则，或临时用 `sudo` 运行部署命令。
   - **macOS**：需安装 `brew install libusb`；注意系统可能会自动挂载相机 MSC 存储卷造成占用，若连接失败请先弹出卷或检查 USB 权限；Apple Silicon 安装 `keystone-engine` 若无对应版本 wheel 需具备 `cmake`/编译环境。（注：作者目前在 Windows + QEMU 完成主要测试，macOS 端仅完成工具链适配代码，尚未在真机 Mac 上实测 USB 注入。）

### 2. 初始化本机编译库与双显载荷（零固件违规，无需满网找固件解包）

> [!TIP]
> **Windows 新手可直接跳过第 2～4 步手动操作**：只要已安装 Python 3.10+、LLVM 与 `Sony-PMCA-RE` 并 `pip install -r requirements.txt`，直接双击 **`一键注入拓展菜单.bat`** 就会自动完成「临时换驱动 → 首次只读初始化 `/lib` 与双显载荷 → 自动编译 → 纯 RAM 部署 → 自动还原原生驱动」。以下分步命令适合二次开发或 macOS/Linux 用户。

因为零固件合规原则，仓库不包含 Sony A7R3 系统 `.so` 与修改后的 `.uxc` 布局字节。现在**只需将你的 Sony A7R3 设为 USB 大容量存储模式 (MSC) 并连接电脑，运行一条只读初始化命令**：

```bash
python tools/a7r3_addon_console.py init-camera
```

该命令会让相机临时进入 Service Mode（需要一次认证握手，并会校验机型是否为 ILCE-7RM3），随后只读完成两件事。读取的文件都会做 MD5 校验；不会写入相机的任何文件或闪存，产出全部放在 `.gitignore` 忽略目录（读取耗时取决于 USB 连接，不做固定时间承诺）：
1. 从相机 `/lib/` 只读拉取并校验 MD5 保存 `libc-2.11.2.so`、`libdl-2.11.2.so`、`libpthread-2.11.2.so` 到 `./firmware/lib/`（编译脚本自动识别，无需手动填写路径）。
2. 从相机 `/usr/share/app/viewStlrec.uxc` 提取生成本地 `src/a7r3_dual_hud_payload.local.h`，用于开启「水平仪与直方图同显」。

*(如果你电脑里已经解压好完整 A7R3 v3.10 固件，也可以不连相机，直接 `cp .env.example .env.local` 并填写 `A7R3_FW_LIB_DIR`；编译时会自动在同级探测 `viewStlrec.uxc` 生成双显载荷。)*

运行环境自检（应看到 `LLVM`、`A7R3 系统库`、`usb`、`pmca` 全部 `[OK]`）：

```powershell
python tools/a7r3_addon_console.py check
```

### 3. 编译插件 `.so`

```powershell
# 编译原生第 7 大类（Route A1，无需生成字体位图，推荐；不带参数时即默认编译它）
python tools/build_addon.py --src src/entries/demo_native_tab7.c --out build/demo_native_tab7_ram.so

```

### 4. 一键全自动注入或启动控制台

```powershell
# 方式 A（Windows 推荐）：双击全自动闭环（自动提权 + 换驱动 + 初始化/编译 + 注入 + 换回原生驱动）
.\一键注入拓展菜单.bat

# 方式 B：命令行一键闭环 / 打开交互式控制台
python tools/a7r3_addon_console.py one-click        # 与双击 一键注入拓展菜单.bat 等效
python tools/a7r3_addon_console.py restore-driver   # 单独恢复相机原生 USB 驱动
.\启动拓展菜单工具.bat                               # 打开交互式控制台

# 方式 C：面向二次开发的单步指令（不自动切换/卸载驱动）
python tools/a7r3_addon_console.py update           # 编译 + 检查 + 安全部署
python tools/a7r3_addon_console.py status           # 查看 10 项钩子状态与日志
python tools/a7r3_addon_console.py quit             # 安全卸载并还原全部原生钩子
```

### 5. 常见报错与排查定位

| 现象 / 提示 | 根因 | 解决方法 |
| --- | --- | --- |
| `未找到完整 LLVM` / macOS 缺 `ld.lld` | 未装完整 LLVM 或误用了 Xcode `/usr/bin/clang` 桩 | Windows 填 `.env.local` 的 `A7R3_LLVM_BIN`；macOS 执行 `brew install llvm` |
| `未找到 A7R3 v3.10 系统库` | 未配置 `A7R3_FW_LIB_DIR` 且 `./firmware/lib/` 为空 | 连上相机跑一次 `python tools/a7r3_addon_console.py init-camera` |
| `未检测到 libusb 原生驱动` / `No backend` / `Access denied` | Windows 未切换到 `libusb-win32`，或 macOS/Linux 缺 libusb/USB 权限 | Windows 请直接运行 **`一键注入拓展菜单.bat`**（自动绑定并在注入后还原，无需 Zadig）；macOS `brew install libusb`；Linux 配 `udev` 或 `sudo` |
| **无法切换到服务模式** (`Timed out waiting for Service Mode` / `MTP`) | 1) 相机停留在 **MTP/PC遥控** 而非 **大容量存储 (MSC)**；<br>2) **Windows 常见**：未用 `one-click` 自动切换，或中途手动中断导致 `054c:0336` 未绑定 `libusb-win32`；<br>3) **macOS/Linux**：系统驱动占用了 MSC 磁盘 | 1) 相机 `MENU -> 设置 -> USB连接` 改 **大容量存储**；<br>2) **Windows**：以管理员运行 `一键注入拓展菜单.bat` 或 `python tools/a7r3_addon_console.py one-click`，会自动为 `0BFF` 与切出后的 `0336` 绑定临时 `libusb-win32` 并在结束后还原；<br>3) **macOS**：`diskutil unmountDisk` 弹出自动挂载卷或装 Sony PMCA DriverLoader kext；<br>4) 换机身直连数据线避免经过劣质 USB Hub |
| `Sony-PMCA-RE` (`pmca`) 缺失 | 未克隆上游 `pmca` 库 | `git clone https://github.com/ma1co/Sony-PMCA-RE.git ../Sony-PMCA-RE` |
| 菜单里「水平仪与直方图同显」**灰色不可选** | 编译时未生成 `src/a7r3_dual_hud_payload.local.h`（安全透传模式） | 运行 `python tools/a7r3_addon_console.py gen-payload --from-camera` 后再 `update` |

### 6. SD 卡自定义音效准备（可选）

在存储卡 `/SHUTTER_SFX/` 目录放入未压缩 PCM `.wav`（推荐 24 kHz 单声道 16-bit PCM，时长 `<= 0.65 s`）：

```powershell
ffmpeg -i input.mp3 -c:a pcm_s16le -ar 24000 -ac 1 shutter_custom.wav
```

---

## 欢迎二次开发与文档导航

- **[免责与合规声明，含相机变砖风险 (DISCLAIMER.md)](DISCLAIMER.md)**
- **[二次开发、Issue 与 PR 规范 (CONTRIBUTING.md)](CONTRIBUTING.md)**
- [给 AI Agent 的环境准备提示词 (docs/AGENT_PROMPT.md)](docs/AGENT_PROMPT.md)
- [架构设计与实现思路 (docs/ARCHITECTURE.md)](docs/ARCHITECTURE.md)
- [A7R3 v3.10 逆向偏移与协议笔记 (docs/FIRMWARE_OFFSETS_V310.md)](docs/FIRMWARE_OFFSETS_V310.md)
- [工具链、编译约束与部署指南 (docs/TOOLS_AND_BUILD.md)](docs/TOOLS_AND_BUILD.md)
- [实机调试踩坑记录与根因分析 (docs/LESSONS_LEARNED.md)](docs/LESSONS_LEARNED.md)
