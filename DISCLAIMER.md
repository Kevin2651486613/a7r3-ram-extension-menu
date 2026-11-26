# 免责声明与开源合规声明 / DISCLAIMER & COMPLIANCE NOTICE

> [!CAUTION]
> **请在使用或二次开发本项目前完整阅读本声明。继续编译、注入或修改本仓库代码即表示你已理解并同意以下全部条款。**
>
> **本项目可能导致相机永久性变砖、硬件损坏、数据丢失与官方保修失效，所有风险由使用者本人完全自担，作者与贡献者不承担任何责任。**

---

## 1. 相机变砖与不可逆硬件损坏特别免责声明（Camera Bricking & Hardware Damage Disclaimer）

1. **本项目不是 Sony 官方工具，也不是受支持的修改方式**：
   - 本仓库通过 USB Service Mode、运行中的 `im.elf` 进程、物理显存 `/dev/mem` 及进程内存读写等**未公开、无官方文档的接口**工作。
   - 尽管仓库设计目标是 **纯内存（Pure-RAM，仅写 `/tmp_bt` tmpfs）、零 Flash/NAND 写入、零 `.text` 代码段补丁**，并内置了机型校验与固件指纹校验（fail-closed），**这些设计与检查只能降低风险，不能保证零风险**。
2. **可能出现的最坏后果**（包括但不限于）：
   - **永久性相机变砖**：无法开机、无法进入正常模式、无法通过官方途径恢复；
   - **主板、快门、传感器或其他硬件模块损坏**；
   - **NVRAM / 出厂校准参数 / 序列号 / 区域设置等数据损坏或丢失**；
   - **存储卡文件系统损坏及照片、视频等数据不可恢复地丢失**；
   - **Sony 官方保修、售后与维修资格失效**；
   - 因相机异常导致的**错过拍摄、商业损失、客户纠纷**等间接损失。
3. **风险更高的典型场景**（这些情况下发生上述后果的可能性显著增加）：
   - 软件缺陷、偏移错配，或相机并非 **ILCE-7RM3 v3.10** 固件（其他区域版本/其他固件版本/其他机型）；
   - 在 Service Mode 或注入过程中 **USB 断开、数据线松动、电量耗尽或意外断电**；
   - 使用 `--force` 绕过机型/固件校验；
   - 二次开发时**引入了对持久分区（Flash/NAND）的写入**、`.text` 补丁或遗漏了钩子还原逻辑；
   - 使用来源不明的第三方修改版代码、`.so` 或脚本。
4. **完全自担风险与零责任承诺**：
   - 本项目作者、维护者与所有贡献者对因使用、修改、分发、调试本仓库任何内容（包括代码、脚本、文档、编译产物）而导致的**相机变砖、硬件损坏、数据丢失、保修失效、拍摄损失及任何直接或间接损失，不承担任何法律、经济、维修或赔偿责任**。
   - 你**必须自行评估风险**，确认自己愿意承担上述全部后果后，才可在自己的设备上使用。
5. **高风险红线清单（使用前请逐条确认）**：
   - 仅在 **ILCE-7RM3 固件 v3.10** 上测试；**切勿对其他机型/固件使用 `--force`**；
   - 测试前将电池**充满电**（建议使用满电电池或可靠的外接供电），使用质量可靠的**机身直连数据线**，避免经过 USB Hub；
   - **切勿在商业委托、婚礼、新闻纪实等不可复现的重要拍摄场景中使用**；
   - 测试前**备份存储卡数据**，建议使用专用于测试的存储卡；
   - 本仓库的所有功能**不应写入** `/setting`、`/system`、`/cert`、`/lens`、`/etc`、`/log` 等持久分区；如发现任何写入这些分区的代码或 PR，请立即停止使用并报告；
   - 出现异常（死机、花屏、无法响应）时，请断开 USB、**取出电池重新上电**，不要反复尝试注入；
   - 二次开发者提交 PR 前，必须遵守 [CONTRIBUTING.md](CONTRIBUTING.md) 中的安全红线。

---

## 2. 测试开发性质与稳定性警告（Experimental R&D Only）

1. **仅供研究与测试开发**：本工程仅用于个人学习、嵌入式 Linux / ARM 逆向方法论交流、纯内存动态挂钩研究及开发者测试。
2. **存在已知或潜在不稳定问题**：项目目前处于早期测试开发（Experimental / Work-in-Progress）阶段，仍可能出现包括但不限于以下异常：
   - 界面局部重绘延迟、闪烁或状态同步滞后；
   - 高速连拍、特殊拍摄/回放模式、特定 SD 卡文件系统状态下的未预期异常或进程无响应（此时需断开电源或重新拔插电池恢复）；
   - 不同相机区域版本或非 **ILCE-7RM3 v3.10** 固件导致的偏移错配崩溃。
3. **禁止用于生产与重要拍摄**：**切勿在商业委托、婚礼、新闻纪实等不可复现的重要拍摄场景中加载本插件。** 如需进行重要拍摄，请务必先安全卸载并重装电池重启相机，确保处于 100% 原厂干净内存状态。

---

## 3. 版权与合规声明（Zero Firmware Policy / Clean-Room Compliance）

为了严格遵守版权法及开源托管平台（GitHub）合规规范，本仓库坚持**零固件内容分发**原则：

1. **绝不包含任何原厂固件资产**：
   - 本仓库**不包含、不托管、不分发**任何 Sony 原厂固件升级包、NAND 镜像、专有 `.so` 动态库文件、固件解包布局二进制数据或商业受版权保护的字体光栅文件。
   - 本仓库中与原厂界面布局（如 Dual-HUD 外层布局修改）或中文点阵字体相关的二进制生成物均已**从代码与 Git 历史中彻底剔除**，仅保留开源的原创工具链脚本、纯 RAM C SDK 框架以及纯文本逆向思路记录。
2. **关于地址偏移与互操作性资料**：
   - [docs/FIRMWARE_OFFSETS_V310.md](docs/FIRMWARE_OFFSETS_V310.md) 与 C 代码中记录的地址偏移、结构布局和协议 ID 仅为实现软件互操作性与纯内存研究而整理的**十六进制地址笔记与接口签名**，不包含任何源自原厂受版权保护的机器码或静态资源拷贝。
3. **本地依赖需用户自行合法获取**：
   - 编译所需的 `libc-2.11.2.so`、`libdl-2.11.2.so`、`libpthread-2.11.2.so` 及可选本地布局载荷（`.local.h`）必须由开发者从**其本人合法拥有的相机设备或官方固件升级包**中自行在本地提取，并严格保留在本地（已通过 `.gitignore` 屏蔽），**切勿将提取出的原厂二进制提交或推送到任何公开仓库，也不要在 Issue / PR 中上传**。

---

## 4. 欢迎社区二次开发（Community Contributions Welcome）

本项目非常欢迎具备嵌入式、逆向分析或图形 UI 开发经验的朋友参与**二次开发、重构优化或适配拓展**：

- 建议拓展方向：完善第 2 页扩展功能、优化触摸手势、多语种字体渲染、更完善的异常保护与状态恢复机制、不同固件版本偏移抽象表等；
- 提交 Issue / PR 前，请先阅读 **[CONTRIBUTING.md](CONTRIBUTING.md)**，并遵守**不提交任何原厂二进制、布局字节数组或私人绝对路径**的合规准则；
- 提交 PR 即表示你已阅读并理解本声明第 1 节，并确认自己在真机上的测试风险由自己承担。

---

## 5. 风险自担与无关联声明

- 本项目按 **“现状（AS IS）”** 提供，依据 [LICENSE](LICENSE)（MIT）不提供任何明示或暗示的担保。因使用、修改或调试本工具造成的设备死机、变砖、数据丢失、存储卡错误或硬件异常，由使用者自行承担全部责任。
- Sony、α / Alpha、ILCE-7RM3、α7R III 等均为 Sony Group Corporation 的注册商标。本项目为独立开源技术研究，与索尼公司没有任何从属、授权或合作关系。

---

## English Summary

- **Camera Bricking / Hardware Damage / Voided Warranty — Use at Your Own Risk**: This project talks to undocumented camera interfaces (USB Service Mode, the live `im.elf` process, `/dev/mem`). Although it is designed to be Pure-RAM (writes only to `/tmp_bt` tmpfs), with no Flash writes and no `.text` patches, **it can still permanently brick your camera, damage hardware, corrupt calibration/NVRAM data or memory cards, lose data, and void Sony's warranty** (especially with software bugs, wrong firmware/model, power or USB loss during Service Mode, `--force`, or faulty modifications). The authors, maintainers and contributors accept **no liability whatsoever**. You use it entirely at your own risk.
- **Experimental Status**: This repository is strictly for **research, education, and experimental development**. It may exhibit UI glitches, instability, or crashes during edge-case camera operations. Do **not** use it for critical or commercial shoots.
- **No Firmware / No Proprietary Assets**: This repository distributes **zero** Sony firmware images, `.so` binaries, firmware-extracted layout payloads, or proprietary font bitmaps. Only original open-source tooling, C overlay SDK framework, and clean-room reverse-engineering notes are included. Users must supply their own legally obtained libraries locally (ignored via `.gitignore`).
- **Contributions Welcome**: Secondary development, improvements, and bug fixes via Issues / PRs are warmly welcomed under the MIT license (see [CONTRIBUTING.md](CONTRIBUTING.md)); no proprietary firmware bytes may be committed.
