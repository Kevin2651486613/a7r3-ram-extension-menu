# 随仓库内置预编译插件 (`prebuilt/`)

本目录随仓库直接附带纯原创、零 Sony 固件字节的预编译插件产物：

- **文件**：`demo_native_tab7_ram.so`
- **对应版本**：`v2.2.1-native-tab7`（源码入口 [`src/entries/demo_native_tab7.c`](../src/entries/demo_native_tab7.c)）
- **文件大小**：`112,604 bytes`
- **SHA-256**：`7fd297d0f570efc02efd135384bb007a64ba0bbee302cba832a65783bdfc3ebc`

## 合规与使用说明

1. **零专有资产**：本 `.so` 完全由本仓库基于 MIT 协议编写的原创 C 源码 freestanding 交叉编译生成，内部**不包含任何** Sony 固件二进制、系统字库或布局资产（水平仪+直方图同显载荷在运行时从用户自己的相机按需提取至 `/tmp_bt/dual_hud_payload.bin`）。
2. **开箱即用**：普通用户直接双击根目录 `一键注入拓展菜单.bat` 时，如果本地 `build/demo_native_tab7_ram.so` 不存在，工具会自动直接使用 `prebuilt/demo_native_tab7_ram.so` 完成纯 RAM 注入——**无需联网拉取 `.so`，也无需安装 LLVM**。
3. **开发者重新编译**：如修改过 `src/` 源码，运行 `python tools/a7r3_addon_console.py build` 或 `一键注入拓展菜单.bat --rebuild` 会编译生成新的 `build/demo_native_tab7_ram.so`；当 `build/demo_native_tab7_ram.so` 存在时将优先使用本地编译产物。
