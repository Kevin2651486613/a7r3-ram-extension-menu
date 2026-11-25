# 工具链、编译约束与部署指南 (Tools, Build & Deployment)

本文档汇总了本项目中使用的全部交叉编译、字模构建、纯 RAM 注入与音频预处理工具，以及在 Windows/Linux 宿主机上的环境配置与操作流程。

---

## 1. 所用工具清单

| 类别 | 工具名称 | 用途说明 |
| --- | --- | --- |
| 交叉编译 & 链接 | **LLVM/Clang 18.1.8 + LLD** (`clang`, `ld.lld`, `llvm-readelf`, `llvm-objdump`, `llvm-nm`) | 编译 freestanding ARMv7-A `.so` 与无 libc 静态助手 ELF，并进行 ELF 符号/重定位段自动化检查 |
| 脚本与控制台 | **Python 3.10+** | 运行编译脚本、字模生成器、纯 RAM 注入器及交互式控制台（依赖见根目录 [requirements.txt](../requirements.txt)） |
| 中文点阵生成 | **Pillow** (`PIL`) + 系统字体 | `tools/gen_md3_assets.py` 生成 4-bit 抗锯齿中文标签表 `src/md3_demo_labels.h`（Windows/macOS/Linux 自动探测） |
| 远程注入与跳板 | **Keystone Engine** (`keystone-engine`) + **内置 MIT `tools/ram_loader/`** | 自包含生成 ARM `dlopen` 跳板、`/proc/<pid>/mem` (`ptrace` 自动回退) 写入器、流式十六进制读取器与终端命令封装 |
| USB 通信层 | **[ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)** (`pmca` + `pyusb` / `libusb`) | 通过 USB Service Mode 与相机建立 BusyBox 终端并传输文件至 `/tmp_bt` |
| 音频转码 | **FFmpeg** (`ffmpeg`) | 将任意音乐/快门声转为机内兼容的 24 kHz 单声道 16-bit 未压缩 PCM `.wav` |
| Windows 启动器 | [`启动拓展菜单工具.bat`](../启动拓展菜单工具.bat) | 自动检测 `python` / `py -3` 并以 UTF-8 启动控制台工具 |

---

## 2. 安装依赖与 `.env.local` 配置（Windows / macOS / Linux）

### 2.1 安装 Python 与系统工具

```bash
# 1. 安装 Python 依赖
pip install -r requirements.txt

# 2. 获取 MIT 协议的 Sony-PMCA-RE（支持 A7R3_PMCA_DIR 或同级目录自动识别）
git clone https://github.com/ma1co/Sony-PMCA-RE.git ../Sony-PMCA-RE
```

- **macOS (Apple Silicon / Intel)**：
  ```bash
  brew install llvm libusb ffmpeg
  # Apple Silicon LLVM 常见路径: /opt/homebrew/opt/llvm/bin
  # Intel Mac LLVM 常见路径:     /usr/local/opt/llvm/bin
  ```
  *(注：若系统在 MSC 模式自动挂载存储卷导致打开失败，需留意先卸载卷或检查 USB 权限；Apple Silicon 安装 `keystone-engine` 需视 Python 版本确保编译/wheel 环境可用。)*
- **Linux (Ubuntu / Debian)**：
  ```bash
  sudo apt install clang lld llvm libusb-1.0-0 ffmpeg
  ```
  *(普通用户需配置 Sony `054c` 的 `udev` 规则或用 `sudo` 运行注入。)*
- **Windows**：安装 LLVM 官方预编译包（含 `clang`、`ld.lld`、`llvm-readelf`）；并使用 **Zadig** 为 Sony 相机接口绑定 **`libusb-win32`**（推荐）或 `libusbK` 驱动（因 `Sony-PMCA-RE` 的 Windows 原生 Service Mode 驱动为占位未实现）。

### 2.2 一键从相机初始化或手动配置 `.env.local`

如果已连接相机，推荐直接运行（只读拉取至被 `.gitignore` 忽略的 `firmware/lib/` 与 `src/a7r3_dual_hud_payload.local.h`，**自动识别，无需手动填写 `.env.local`**）：

```powershell
python tools/a7r3_addon_console.py init-camera
```

本仓库**不包含任何本机绝对路径或原厂系统库文件**。若使用本机已有固件目录，`tools/` 下的脚本统一通过 `tools/env_config.py` 读取根目录 `.env.local`（已被 `.gitignore` 忽略）：

```bash
cp .env.example .env.local
```

| 环境变量 | 示例值 | 说明 |
| --- | --- | --- |
| `A7R3_LLVM_BIN` | `/opt/homebrew/opt/llvm/bin` | 包含 `clang`、`ld.lld`、`llvm-readelf` 的目录（未设置时自动查找 `PATH` 中的 `clang`） |
| `A7R3_FW_LIB_DIR` | `/path/to/your/a7r3_v310_extraction/lib` | 用户自行从合法持有的设备/固件中本地提取的 `/lib`（仅编译 `.so` 时需要 `libc-2.11.2.so`、`libdl-2.11.2.so`、`libpthread-2.11.2.so`） |
| `A7R3_UXC_PATH` | `/path/to/.../usr/share/app/viewStlrec.uxc` | 可选：本地 `viewStlrec.uxc` 路径；未填时会在 `A7R3_FW_LIB_DIR` 相邻目录自动探测生成 `src/a7r3_dual_hud_payload.local.h` |
| `A7R3_PMCA_DIR` | `/path/to/Sony-PMCA-RE` | 若已克隆至 `./Sony-PMCA-RE` 或 `../Sony-PMCA-RE`，或通过 pip 安装，可留空自动识别 |
| `A7R3_FONT_BOLD` / `A7R3_FONT_REG` | `/System/Library/Fonts/PingFang.ttc` | 仅重新生成 MD3 中文点阵字模时使用（Route A1 原生第 7 大类不需要此项） |

---

## 3. Freestanding ARMv7-A 编译约束

由于目标相机运行在古老的 **ARM Linux 3.0 + glibc 2.11.2** 环境下，编译选项与链接校验必须严格遵循 [tools/build_addon.py](../tools/build_addon.py) 的规则：

1. **核心编译参数**：
   - `--target=arm-linux-gnueabi -march=armv7-a -mfloat-abi=soft -shared -fPIC -O2`
   - `-nostdlib -ffreestanding -fno-builtin -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -fvisibility=hidden`
   - `-Wall -Wextra -Werror -Wno-unused-parameter`（强制零警告编译）
2. **关键链接参数（防 `ld.so` 崩溃）**：
   - `-fuse-ld=ld.lld -Wl,-z,now -Wl,-Bsymbolic -Wl,-z,nocombreloc -Wl,--no-as-needed`
   - **必须加 `-Wl,-z,nocombreloc`**：`ld.lld` 默认合并重定位并生成 `DT_RELCOUNT`，而 A7R3 固件的 glibc 2.11.2 动态链接器在处理带 `DT_RELCOUNT` 的 `BIND_NOW` 共享库时会直接崩溃。`build_addon.py` 在链接后会自动检查 `llvm-readelf -d` 是否误输出 `(RELCOUNT)`。
3. **禁止引入隐式运行时/除法辅助符号**：
   - 由于启用了 `-nostdlib` 且未链接 `libgcc`，所有 32/64 位整数除法不得在高频/可避开路径产生 `__aeabi_uidiv` / `__aeabi_idiv` / `__aeabi_uldivmod` 等未解析符号（例如 [a7r3_shutter_sfx.c](../src/a7r3_shutter_sfx.c) 与 [a7r3_pb_focus.c](../src/a7r3_pb_focus.c) 中专门实现了按位整型除法 `u64_div_u32` 和 `udiv32`）。
   - `build_addon.py` 会交叉比对 `libc-2.11.2.so`、`libdl-2.11.2.so`、`libpthread-2.11.2.so` 的导出表；实机产物的 `UND` 符号应仅允许 `WEAK pthread_create`。

### 常用构建命令

```powershell
# 编译原生第 7 大类 + 纯内存菜单触摸版（Route A1，当前主线推荐）
python tools/build_addon.py --src src/demo_native_tab7.c --out build/demo_native_tab7_ram.so

# 编译 MD3 自绘菜单完整版（v1.0-md3 路线：原生设置第 7 页「拓展菜单」入口）
python tools/build_addon.py --src src/demo_cert_entry.c --out build/demo_cert_entry_ram.so

# 如修改了 MD3 中文菜单文本（SPECS），重新生成 md3_demo_labels.h
python tools/gen_md3_assets.py

# 手动从本地 viewStlrec.uxc 或已连接相机只读生成受忽略的本地 Dual-HUD 载荷
python tools/gen_dual_hud_payload.py --from-camera
```

---

## 4. 控制台管理工具与部署流程

`tools/a7r3_addon_console.py` 封装了完整的编译、USB Service Mode 握手、纯 RAM 注入、状态检查与无痕卸载流程：

```powershell
# 交互式控制台（或直接双击 启动拓展菜单工具.bat）
python tools/a7r3_addon_console.py

# 命令行一键模式：
python tools/a7r3_addon_console.py check    # 离线检查 Python/环境变量/工具链/.so 边界有效性（无需连接相机）
python tools/a7r3_addon_console.py build    # 交叉编译当前默认产物 demo_native_tab7_ram.so
python tools/a7r3_addon_console.py deploy   # 安全停掉旧实例 + 边界检查 + 部署当前 .so + 检查 10 项钩子
python tools/a7r3_addon_console.py update   # 先重新编译再热部署（开发迭代最常用）
python tools/a7r3_addon_console.py status   # 查看 PID、10 项钩子状态与最近 25 行 /tmp_bt/addon.log
python tools/a7r3_addon_console.py open     # 兼容保留（主要供 MD3 版弹窗遥控测试；原生 Tab 7 按 MENU 即见）
python tools/a7r3_addon_console.py quit     # 安全还原所有 GOT/vptr 钩子并退出工作线程
python tools/a7r3_addon_console.py gen-payload --from-camera  # 从已连接相机生成本地 Dual-HUD 载荷
python tools/a7r3_addon_console.py init-camera                # 一次性从相机只读初始化 /lib 三件套 + Dual-HUD 载荷
```

部署成功时，日志与自检输出应全部显示 `[OK]`：
- `SDK_TAB7_ALIVE`（后台工作线程存活）
- `SDK_TAB7_TEXT_HOOK=0x1`（原生 `TextSprite` UTF-8 渲染钩子）
- `SDK_TAB7_MGR_HOOK=0x1`（`CmnViewProcessDataMgr` 纯内存虚表钩子）
- `SDK_TAB7_MLT_HOOK=0x1`（`PAS_MenuListTab` 第 7 大类排版钩子）
- `SDK_TAB7_INSTALLED=0x1`（原生第 7 大类设置树节点挂载）
- `SDK_EV_HOOKS=0x1`（曝光补偿波轮屏蔽钩子）
- `SDK_DUAL_HOOKS=0x1`（水平仪与直方图同显模块）
- `SDK_PB_HOOKS=0x1`（相册显示焦点位置钩子）
- `SDK_SFX_INSTALL_OK`（自定义电子快门音效钩子）
- `SDK_MTOUCH_HOOKS=0x1`（原生菜单触摸控制钩子）

---

## 5. SD 卡自定义快门音效（`.wav`）准备规范

1. 在存储卡（卡 1 或卡 2 根目录）创建目录 **`/SHUTTER_SFX/`**（例如 `X:\SHUTTER_SFX\`）。
2. 将音效文件放入该目录（扩展名 `.wav` 即可，支持长文件名，最多自动扫描前 16 个文件）。
3. **格式要求**：
   - 容器格式：标准 RIFF `WAVE`（**必须为未压缩 PCM `audio_fmt = 1` 或 `WAVE_FORMAT_EXTENSIBLE` 封装 PCM**，不支持 MP3-in-WAV `0x55`、ADPCM 等压缩编码）；
   - 采样率：`4000 Hz ~ 96000 Hz`（推荐直接设为 **`24000 Hz`**，与机内 ADF 硬件一致，可避免重采样损益）；
   - 位深与声道：`8 / 16 / 24-bit` PCM，单声道或立体声；
   - 建议时长：`<= 0.65 s`（受限于 `ADF_ENC_CAP = 32768 B`，超过部分会被截断）。
4. **推荐使用 `ffmpeg` 批量转码**：

```powershell
ffmpeg -i source_sound.mp3 -c:a pcm_s16le -ar 24000 -ac 1 shutter_01.wav
```

5. 在相机上打开「拓展菜单」→ 第 4 行「自定义快门音效」→ 点击「刷新SD」即可动态加载并试听。
