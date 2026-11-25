# Sony ILCE-7RM3 (A7R3 v3.10) 原生第 7 大类拓展菜单（Route A1 分支）

[![Branch](https://img.shields.io/badge/Branch-feat%2Fnative--tab7--a1%20(Route%20A1)-purple.svg)]()
[![Status](https://img.shields.io/badge/Status-Experimental%20%2F%20R%26D%20Only-orange.svg)]()
[![Compliance](https://img.shields.io/badge/Firmware-Zero%20Proprietary%20Assets-blue.svg)]()
[![Safety](https://img.shields.io/badge/Mode-Pure%20RAM%20(%2Ftmp__bt)-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)]()

> [!NOTE]
> **版本与分支说明**：
> - Tag `v1.0-md3`：早期 Material Design 3 自绘卡片菜单版（通过劫持设置第 7 页「认证标识」弹出；`src/demo_cert_entry.c` 仍可在当前分支单独编译）。
> - **`main` / `feat/native-tab7-a1`（当前最新，Route A1 主线）**：**原生索尼 UI 第 7 大类 + 纯内存菜单触摸版**——无需任何全屏显存自绘与防闪烁守卫，直接在原厂 `MENU` 顶栏新增第 7 个大类 Tab，使用原厂 GPU 渲染与索尼中文字库、原生二级选择弹窗与拨轮/按键交互，内置第 1 页第 4 项可开关的 **「菜单触摸控制」**；已完整内置 MIT 协议纯内存注入助手与 `requirements.txt`（支持 Windows / macOS / Linux），并修复冷启动后动态堆区扩增导致的 `/proc/self/maps` 缓存过期问题。

> [!WARNING]
> **测试开发与合规声明（必读）**：
> 1. **仅限测试开发**：本工程与分支为 **实验性（Experimental）研究与二次开发框架**，当前仍存在潜在不稳定与边界兼容性风险，**切勿用于商业或重要拍摄**；**欢迎社区基于本框架进行二次开发与改进**。
> 2. **零固件 / 合规仓库**：为遵守开源与版权规范，本仓库**已彻底剔除**所有 Sony 原厂固件二进制、原厂布局数据字节（`a7r3_dual_hud_payload` 已解耦为本地可选文件）及预生成字体位图，Route A1 亦**不包含任何 `.uxc` 修改后的布局二进制**，仅通过纯 C 逻辑动态调用原厂控件，详情请阅读 **[DISCLAIMER.md](DISCLAIMER.md)**。

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
| **原生第 7 大类 UI（Route A1 主线）** | [a7r3_native_tab7.c](src/a7r3_native_tab7.c) / [demo_native_tab7.c](src/demo_native_tab7.c) | 扩展 `viewUnified2` 活动设置树（`0x00a51c60`）为 7 个大类（2 页 6+4 共 10 项原厂条目）；后置定位 `{533, 0, 91, 66}` 并修复顶栏底框 `0x020cc146`；`GEN_Icon::setScalingFlag` + `setImageFilter` 将 `105×66` 底色缩放入 `91×66` 槽位，并用运行时 `ResourceManager::getImageSize` 居中对齐前景图标 `0x0205cb79`；托管 `proc_id (0x7f01..0x7f0a)` 与 `TextSprite` UTF-8 解析 |
| **原生菜单纯内存触摸控制** | [a7r3_menu_touch.c](src/a7r3_menu_touch.c) / [a7r3_input.c](src/a7r3_input.c) | **零 `.text` 代码补丁、零堆扫描**；在第 7 大类第 1 页第 4 项提供开关；通过可写虚表追踪 `ViewSettingMenu`、`PAS_MenuListTab`、自定义按键列表/功能页及白平衡专用页；支持 7 大类一步直达、标题栏翻页、6 行直达打开、二级弹窗（含滚动列表与框外取消）、不可设定提示全屏点击返回与白平衡左侧滚轮点选 |
| 原生菜单无痕入口 + MD3 双页 UI（兼容保留） | [a7r3_entry_cert.c](src/a7r3_entry_cert.c) / [demo_cert_entry.c](src/demo_cert_entry.c) | 解除隐藏并切换「认证标识 (`0x10f`)」`vptr`，在纯 RAM 显存绘制 MD3 双页卡片（位于 `main` / `v1.0-md3`，仍可单独编译） |
| 屏蔽曝光补偿波轮 | [a7r3_input.c](src/a7r3_input.c) | 钩住 `osal_rcv_msg_tmo`、`get_phy` / `get_ev` 并拦截 `0x70d2` / `0x70d3`，同时提供 OSAL 队列按键注入与非吞噬式触控释放监听 |
| 水平仪与直方图同显（可选本地载荷） | [a7r3_dual_hud.c](src/a7r3_dual_hud.c) | 钩住 `getResID` / `RecDtor` / `UserGetLayoutMode` / 路由槽；**默认无载荷透传编译**，用户可自行提供本地提取修改的 `src/a7r3_dual_hud_payload.local.h` 启用 |
| 相册回放显示对焦点 | [a7r3_pb_focus.c](src/a7r3_pb_focus.c) | `FeaCore` + `PlayStillSingle` 捕获 EXIF，机内解析 Sony MakerNote (`0x2027`) 对焦坐标并叠绘像素级无损红框，兼容连拍组/DISP/缩放/MENU 退让 |
| 电子快门自定义快门音效 | [a7r3_shutter_sfx.c](src/a7r3_shutter_sfx.c) | 钩住 `ObjStillRecorder::ParseRecCntInfoPinCommand`；使用线程安全 `Backup_read(0x01070b2c)` 判定静音状态；绕开 8.3 限制调用 `PrFILE_fsfirst`/`pf2_fopen` 扫描 `/SHUTTER_SFX/*.wav`；机内 WAV→Sony 32B BFP 编码 + `futex` + `DataflowInfraCh::sndAsyncDataflowMsg` 异步发声 |

---

## 仓库目录结构

```text
a7r3-ram-extension-menu/
├── README.md                          # 项目说明（本文件）
├── DISCLAIMER.md                      # 测试开发声明、稳定性警告与开源合规声明（必读）
├── LICENSE                            # MIT 开源协议
├── CREDITS.md                         # 致谢与外部依赖说明
├── requirements.txt                   # Python 构建与 USB 注入依赖清单（跨平台）
├── .env.example                       # 环境变量模板（复制到 .env.local 进行本机配置）
├── .gitignore                         # 忽略 build/、.env.local、*.local.h、生成字模等
├── 启动拓展菜单工具.bat                # Windows 控制台一键启动入口
├── src/                               # 纯 RAM C SDK 与功能模块源码（无任何原厂二进制）
│   ├── a7r3_dual_hud_payload.local.h.example # 可选本地 Dual-HUD 载荷模板（零字节）
│   ├── ...                            # 各功能模块 (.c/.h)
├── tools/
│   ├── env_config.py                  # 加载可选 .env.local 环境变量
│   ├── a7r3_addon_console.py          # 交互式 / CLI 控制台（含离线自检 check 与安全部署）
│   ├── build_addon.py                 # freestanding 编译、自动生成字模与 ELF 严格校验
│   ├── deploy_ram.py                  # 带多重边界校验的纯 RAM (/tmp_bt) 注入器
│   ├── gen_md3_assets.py              # 从本机系统字体实时生成 4-bit 中文点阵头文件
│   └── ram_loader/                    # 纯 RAM 注入助手（全内置 MIT 实现）
│       ├── build_ram_tools.py         # 远程 SVC 与 ARM dlopen 跳板生成器
│       ├── terminal_shell.py          # USB Service Terminal BusyBox 命令收发器
│       ├── build_mem_writer.py        # /proc/<pid>/mem 与 ptrace 双路径写入器
│       └── build_show_hex.py          # 流式只读内存/文件十六进制读取器
└── docs/
    ├── ARCHITECTURE.md                # 总体架构、纯 RAM 注入及各模块实现思路
    ├── FIRMWARE_OFFSETS_V310.md       # A7R3 v3.10 互操作性地址偏移与通信协议笔记
    ├── TOOLS_AND_BUILD.md             # 工具链配置、编译约束、控制台命令与 WAV 转码
    └── LESSONS_LEARNED.md             # 实机调试踩坑记录与根因分析
```

---

## 快速开始

### 1. 安装依赖与配置环境（支持 Windows / macOS / Linux）

```bash
pip install -r requirements.txt
git clone https://github.com/ma1co/Sony-PMCA-RE.git ../Sony-PMCA-RE
# macOS 还需安装 LLVM 与 libusb: brew install llvm libusb
```

复制 `.env.example` 为 `.env.local`（已被 `.gitignore` 忽略）并填写路径：

```bash
# Windows: copy .env.example .env.local
cp .env.example .env.local
# 填写 A7R3_LLVM_BIN、合法提取的 A7R3 v3.10 /lib（含 libc/libdl/libpthread 2.11.2），
# 以及 A7R3_PMCA_DIR（若放在 ./Sony-PMCA-RE 或 ../Sony-PMCA-RE 可自动识别）
```

运行离线边界与依赖自检：

```powershell
python tools/a7r3_addon_console.py check
```

### 2. 编译插件 `.so`

```powershell
# 编译原生第 7 大类（Route A1，无需生成字体位图，推荐）
python tools/build_addon.py --src src/demo_native_tab7.c --out build/demo_native_tab7_ram.so

# 若需同时编译 MD3 自绘菜单版：
python tools/build_addon.py --src src/demo_cert_entry.c --out build/demo_cert_entry_ram.so
```

### 3. 启动控制台并纯 RAM 注入（开发测试用）

```powershell
# 方式 A：双击 Windows 入口
.\启动拓展菜单工具.bat

# 方式 B：命令行快捷指令
python tools/a7r3_addon_console.py update   # 编译 + 检查 + 安全部署
python tools/a7r3_addon_console.py status   # 查看 10 项钩子状态与日志
python tools/a7r3_addon_console.py quit     # 安全卸载并还原全部原生钩子
```

### 4. SD 卡自定义音效准备（可选）

在存储卡 `/SHUTTER_SFX/` 目录放入未压缩 PCM `.wav`（推荐 24 kHz 单声道 16-bit PCM，时长 `<= 0.65 s`）：

```powershell
ffmpeg -i input.mp3 -c:a pcm_s16le -ar 24000 -ac 1 shutter_custom.wav
```

---

## 欢迎二次开发与文档导航

- **[免责与合规声明 (DISCLAIMER.md)](DISCLAIMER.md)**
- [架构设计与实现思路 (docs/ARCHITECTURE.md)](docs/ARCHITECTURE.md)
- [A7R3 v3.10 逆向偏移与协议笔记 (docs/FIRMWARE_OFFSETS_V310.md)](docs/FIRMWARE_OFFSETS_V310.md)
- [工具链、编译约束与部署指南 (docs/TOOLS_AND_BUILD.md)](docs/TOOLS_AND_BUILD.md)
- [实机调试踩坑记录与根因分析 (docs/LESSONS_LEARNED.md)](docs/LESSONS_LEARNED.md)
