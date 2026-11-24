# 免责声明与开源合规声明 / DISCLAIMER & COMPLIANCE NOTICE

> [!CAUTION]
> **请在使用或二次开发本项目前完整阅读本声明。继续编译、注入或修改本仓库代码即表示你已理解并同意以下全部条款。**

---

## 1. 测试开发性质与稳定性警告（Experimental R&D Only）

1. **仅供研究与测试开发**：本工程仅用于个人学习、嵌入式 Linux / ARM 逆向方法论交流、纯内存动态挂钩研究及开发者测试。
2. **存在已知或潜在不稳定问题**：项目目前处于早期测试开发（Experimental / Work-in-Progress）阶段，仍可能出现包括但不限于以下异常：
   - 界面局部重绘延迟、闪烁或状态同步滞后；
   - 高速连拍、特殊拍摄/回放模式、特定 SD 卡文件系统状态下的未预期异常或进程无响应（此时需断开电源或重新拔插电池恢复）；
   - 不同相机区域版本或非 **ILCE-7RM3 v3.10** 固件导致的偏移错配崩溃。
3. **禁止用于生产与重要拍摄**：**切勿在商业委托、婚礼、新闻纪实等不可复现的重要拍摄场景中加载本插件。** 如需进行重要拍摄，请务必先安全卸载并重装电池重启相机，确保处于 100% 原厂干净内存状态。

---

## 2. 版权与合规声明（Zero Firmware Policy / Clean-Room Compliance）

为了严格遵守版权法及开源托管平台（GitHub）合规规范，本仓库坚持**零固件内容分发**原则：

1. **绝不包含任何原厂固件资产**：
   - 本仓库**不包含、不托管、不分发**任何 Sony 原厂固件升级包、NAND 镜像、专有 `.so` 动态库文件、固件解包布局二进制数据或商业受版权保护的字体光栅文件。
   - 本仓库中与原厂界面布局（如 Dual-HUD 外层布局修改）或中文点阵字体相关的二进制生成物均已**从代码与 Git 历史中彻底剔除**，仅保留开源的原创工具链脚本、纯 RAM C SDK 框架以及纯文本逆向思路记录。
2. **关于地址偏移与互操作性资料**：
   - [docs/FIRMWARE_OFFSETS_V310.md](docs/FIRMWARE_OFFSETS_V310.md) 与 C 代码中记录的地址偏移、结构布局和协议 ID 仅为实现软件互操作性与纯内存研究而整理的**十六进制地址笔记与接口签名**，不包含任何源自原厂受版权保护的机器码或静态资源拷贝。
3. **本地依赖需用户自行合法获取**：
   - 编译所需的 `libc-2.11.2.so`、`libdl-2.11.2.so`、`libpthread-2.11.2.so` 及可选本地布局载荷（`.local.h`）必须由开发者从**其本人合法拥有的相机设备或官方固件升级包**中自行在本地提取，并严格保留在本地（已通过 `.gitignore` 屏蔽），**切勿将提取出的原厂二进制提交或推送到任何公开仓库**。

---

## 3. 欢迎社区二次开发（Community Contributions Welcome）

本项目非常欢迎具备嵌入式、逆向分析或图形 UI 开发经验的朋友参与**二次开发、重构优化或适配拓展**：

- 建议拓展方向：完善第 2 页扩展功能、优化触摸手势、多语种字体渲染、更完善的异常保护与状态恢复机制、不同固件版本偏移抽象表等；
- 提交 PR / Fork 时，请同样遵守**不提交任何原厂二进制、布局字节数组或私人绝对路径**的合规准则。

---

## 4. 风险自担与无关联声明

- 本项目按 **“现状（AS IS）”** 提供，依据 [LICENSE](LICENSE)（MIT）不提供任何明示或暗示的担保。因使用、修改或调试本工具造成的设备死机、数据丢失、存储卡错误或硬件异常，由使用者自行承担全部责任。
- Sony、α / Alpha、ILCE-7RM3、α7R III 等均为 Sony Group Corporation 的注册商标。本项目为独立开源技术研究，与索尼公司没有任何从属、授权或合作关系。

---

## English Summary

- **Experimental Status**: This repository is strictly for **research, education, and experimental development**. It may exhibit UI glitches, instability, or crashes during edge-case camera operations. Do **not** use it for critical or commercial shoots.
- **No Firmware / No Proprietary Assets**: This repository distributes **zero** Sony firmware images, `.so` binaries, firmware-extracted layout payloads, or proprietary font bitmaps. Only original open-source tooling, C overlay SDK framework, and clean-room reverse-engineering notes are included. Users must supply their own legally obtained libraries locally (ignored via `.gitignore`).
- **Contributions Welcome**: Secondary development, improvements, and bug fixes via Issues / PRs are warmly welcomed under the MIT license (provided no proprietary firmware bytes are committed).
