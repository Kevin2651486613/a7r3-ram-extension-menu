> 本仓库当前仅持续维护原生第 7 大类（Tab7 / Route A1）主线；旧版 MD3 路线（`src/legacy/` / `v1.0-md3`）已暂停开发。

## 变更摘要

<!-- 简述本 PR 做了什么、为什么 -->

## 关联 Issue

<!-- 例如：Closes #123 -->

## 变更类型与涉及层级

- [ ] `fix`：缺陷修复
- [ ] `feat`：新功能
- [ ] `refactor`：重构（不改变行为）
- [ ] `docs`：文档
- [ ] 其他：

涉及目录：

- [ ] `src/core/`（若勾选，需确认 Tab7 主线重新编译通过）
- [ ] `src/modules/`
- [ ] `src/entries/`
- [ ] `tools/`
- [ ] `docs/` / `.github/`

## 测试与验证记录

离线验证（请粘贴关键输出或写明结果）：

- [ ] `python -I -m py_compile` 已对改动的 `.py` 文件通过
- [ ] `python tools/a7r3_addon_console.py check` 通过
- [ ] `python tools/build_addon.py`（`src/entries/demo_native_tab7.c`）编译通过且零未解析符号

真机验证（二选一，请如实勾选）：

- [ ] 已在 **ILCE-7RM3 v3.10** 真机上测试，测试内容：
- [ ] **硬件未测试**（仅离线编译验证）

## 安全与合规自查清单

- [ ] 仅写入 `/tmp_bt`（tmpfs），没有写入任何 Flash/NAND 持久分区
- [ ] 没有 `.text` 代码段补丁（仅 `.got` 槽位替换或可写虚表 + `vptr` 切换）
- [ ] 没有调用 `dlclose()`；新增钩子均有对称的还原逻辑
- [ ] 没有削弱 `check_camera_model()` / `patch_viewstlrec()` 的 fail-closed 检查，`--force` 没有被设为默认
- [ ] 没有提交 Sony 固件、`.so`、`*.local.h`、`md3_demo_labels.h`、`.env.local`、`firmware/` 内容或个人绝对路径
- [ ] `一键注入拓展菜单.bat` 与 `启动拓展菜单工具.bat` 行尾仍为 CRLF（若改动过）

## 相机变砖与风险免责确认

- [ ] 我已阅读 [DISCLAIMER.md](../DISCLAIMER.md) 与 [CONTRIBUTING.md](../CONTRIBUTING.md)，理解本项目可能导致相机变砖、硬件损坏、数据丢失与保修失效，并确认测试风险由我自行承担，项目作者与维护者不承担任何责任。
