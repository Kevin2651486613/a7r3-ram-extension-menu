# 贡献指南 / CONTRIBUTING

感谢你愿意参与 Sony ILCE-7RM3（α7R III，固件 v3.10）纯内存拓展菜单工程的二次开发。提交 Issue 或 Pull Request 前，请完整阅读本文。

---

## 1. 开发前必读：相机变砖免责声明与安全红线

> [!CAUTION]
> 本项目会通过 USB Service Mode 与运行中的相机进程交互，**存在导致相机永久变砖、硬件损坏、数据丢失、保修失效的风险**。在自己的设备上测试，意味着你自行承担全部风险。作者、维护者与贡献者不承担任何责任。详见 **[DISCLAIMER.md](DISCLAIMER.md)**。

提交 Issue / PR 即表示你已阅读并理解 [DISCLAIMER.md](DISCLAIMER.md) 第 1 节。

### 1.1 硬性规则（违反任意一条的 PR 将被拒绝）

1. **仅纯内存（Pure-RAM）**：运行时文件与日志只能写入 `/tmp_bt`（tmpfs）。**禁止**向 Flash/NAND 持久分区（`/setting`、`/system`、`/cert`、`/lens`、`/etc`、`/log` 等）写入。
2. **零 `.text` 代码段补丁**：只允许原子替换可写 `.got` 槽位，或复制 C++ 虚表到可写数组并切换 `vptr`。
3. **禁止调用 `dlclose()`**；每个钩子必须提供对称的卸载/还原函数（`*_remove()` / `*_uninstall()`），并在卸载标记中输出还原日志（参见 `tools/a7r3_addon_console.py` 的 `RESTORE_MARKERS`）。
4. **不得削弱或绕过 fail-closed 安全检查**：
   - `tools/deploy_ram.py` 的 `check_camera_model()`：读取机型失败或机型不是 ILCE-7RM3 时必须拒绝继续，**不得把 `--force` 设为默认**；
   - `tools/gen_dual_hud_payload.py` 的 `patch_viewstlrec()`：SHA-256 指纹校验不匹配必须报错退出，**不得添加绕过开关**。
5. **零专有资产**：**不得提交** Sony 固件、`.so` 库、固件提取物、`src/*.local.h`、`src/md3_demo_labels.h`、`.env.local`、`firmware/` 目录内容，或含个人绝对路径、序列号、账号信息的日志与截图。

### 1.2 真机测试建议

- 使用满电电池、质量可靠的机身直连数据线（避免 USB Hub）和专用测试存储卡，并提前备份数据。
- 仅在 ILCE-7RM3 v3.10 上测试；**不要对其他机型/固件使用 `--force`**。
- 出现死机或花屏时，断开 USB 并取出电池，不要反复注入。

---

## 2. 仓库架构与二次开发指南

> [!NOTE]
> **维护范围**：所有二次开发、Bug 修复与 PR 请基于当前持续维护的 **Tab7 主线**（`feat/native-tab7-a1` / `main`）。MD3 路线（Tag `v1.0-md3`，源码在 `src/legacy/`）已**暂停开发**，暂不接受针对 MD3 的新功能 PR。

```text
a7r3-ram-extension-menu/
├── src/
│   ├── core/            # 核心 SDK：系统调用、显存、绘制、输入钩子
│   ├── modules/         # 功能模块：第 7 大类、菜单触摸、双显、回放对焦、快门音效
│   ├── entries/         # 入口文件：demo_native_tab7.c（Tab7 主线）
│   ├── legacy/          # 已暂停开发的 MD3 / 早期示例归档（默认不参与编译，勿在此新增功能）
│   └── *.local.h        # 本地生成的头文件（已被 .gitignore 忽略，勿提交）
├── tools/               # 构建、部署、控制台脚本；ram_loader/ 为纯 RAM 注入助手
├── docs/                # 架构、偏移笔记、工具链、踩坑记录、Agent 提示词
├── .github/             # Issue / PR 模板
└── experiments/         # 本地实验（已被 .gitignore 忽略，不属于主线）
```

| 层级 | 目录 | 说明 |
| --- | --- | --- |
| 核心 | `src/core/` | 不依赖具体功能；改动影响所有入口，必须重新编译全部入口验证 |
| 模块 | `src/modules/` | 每个功能一对 `.c/.h`，提供 `install/remove/tick` 风格接口 |
| 入口 | `src/entries/` | 组装模块、创建工作线程、监听 `/tmp_bt/addon_quit` 门控并按序卸载 |
| 归档 | `src/legacy/` | MD3 路线与早期示例，仅保留；不属于主线，不参与默认构建 |
| 工具 | `tools/` | Python 3.10+；`build_addon.py` 维护 `CORE_SRCS` / `MODULE_SRCS` 编译清单 |

### 2.1 新增一个功能模块

1. 在 `src/modules/` 新建 `a7r3_<feature>.c` 与 `.h`，实现安装与还原函数（还原必须恢复全部 GOT/vptr 槽位）。
2. 在 `tools/build_addon.py` 的 `MODULE_SRCS` 中登记新 `.c` 文件。
3. 在入口文件（如 `src/entries/demo_native_tab7.c`）中调用安装函数，并在 `GATE_QUIT` 卸载流程中按安装的相反顺序调用还原函数。
4. 如需在控制台里检查钩子状态，在 `tools/a7r3_addon_console.py` 的 `HOOK_CHECKS` / `RESTORE_MARKERS` 中添加日志标记。
5. 在 `docs/ARCHITECTURE.md` 补充实现思路；偏移与地址写入 `docs/FIRMWARE_OFFSETS_V310.md`。

### 2.2 C 代码约束（freestanding ARMv7-A）

- 编译选项为 `-Wall -Wextra -Werror`，必须零警告。
- 未链接 `libgcc`：避免产生 `__aeabi_uidiv`、`__aeabi_idiv`、`__aeabi_uldivmod` 等隐式除法辅助符号（参考 `a7r3_pb_focus.c` / `a7r3_shutter_sfx.c` 中的手写除法）。
- 保持 `-Wl,-z,nocombreloc`；产物未解析符号只允许 `WEAK pthread_create`（`build_addon.py` 会自动校验）。
- `一键注入拓展菜单.bat` 与 `启动拓展菜单工具.bat` 必须保持 **CRLF** 行尾。

---

## 3. 规范化 Issue 提出指南

请使用 `.github/ISSUE_TEMPLATE/` 中的模板（Bug 反馈 / 功能提案）。提交前先搜索已有 Issue，并在 [README.md](README.md) 的「常见报错与排查定位」中确认问题尚无现成解法。

Bug 反馈必须提供：

- 相机型号与固件版本（以及是否为官方 v3.10）；
- 主机操作系统、Python 版本、LLVM 版本、`pmca` 来源（内置 `tools/vendor/pmca/` 或自定义 `A7R3_PMCA_DIR`）；
- USB 驱动情况（Windows：`python tools/win_usb_driver.py status` 输出，如原生 `USBSTOR` / 临时 `libusb0`；Linux：`udev`；macOS：libusb）；
- `python tools/a7r3_addon_console.py check` 的完整输出；
- 脱敏后的 `/tmp_bt/addon.log` 相关片段（`python tools/a7r3_addon_console.py status`）；
- 最小复现步骤、预期行为与实际行为。

> [!WARNING]
> **不要在 Issue 中上传** Sony 固件、`.so` 库、`*.local.h`、`.env.local` 或含个人路径的未脱敏日志。

---

## 4. 规范化 Pull Request 提交指南

### 4.1 分支与提交信息

- 分支命名：`feat/<说明>`、`fix/<说明>`、`docs/<说明>`、`refactor/<说明>`。
- 提交信息使用 Conventional Commits，例如 `fix(pb-focus): suppress reticle over native dialogs`。
- 一个 PR 只做一件事；纯重构（移动文件）与功能改动请分开提交。

### 4.2 提交前必须在本地运行

```powershell
python -I -m py_compile tools/a7r3_addon_console.py tools/win_usb_driver.py tools/build_addon.py tools/deploy_ram.py tools/gen_dual_hud_payload.py
python tools/a7r3_addon_console.py check
python tools/build_addon.py        # 默认：src/entries/demo_native_tab7.c
```

凡改动 `src/core/` 或 `src/modules/`，必须确认 Tab7 入口编译通过且无未解析符号。

### 4.3 如实披露测试情况

PR 描述中必须明确写出：

- 是否在真机 **ILCE-7RM3 v3.10** 上测试过，测试了哪些操作；
- 仅离线编译验证的，请写明 **「硬件未测试」**。

请不要声称未实际执行过的测试已通过。

### 4.4 PR 合规检查

PR 模板中的安全与合规清单需要逐项勾选，涵盖纯内存、零 `.text` 补丁、对称还原、fail-closed 检查未被削弱、零专有资产，以及对 [DISCLAIMER.md](DISCLAIMER.md) 的确认。

---

## 5. 许可证

向本仓库提交的内容默认以 [MIT](LICENSE) 协议授权。
