# Windows 临时驱动命令行助手 (`wdi-simple.exe`)

- **用途**：供 [`tools/win_usb_driver.py`](../win_usb_driver.py) 在 Windows 端以静默子进程方式调用，为已连接的 Sony 相机（`054C:0BFF` MSC / `054C:0336` & `054C:02A9` Service Mode）临时绑定 `libusb-win32`（`libusb0`）驱动；纯 RAM 注入完成后由 `pnputil` 卸载并恢复原生 `USBSTOR`。
- **上游来源**：[pbatard/libwdi](https://github.com/pbatard/libwdi) Tag `v1.5.1`（`examples/wdi-simple.c`）
- **内嵌驱动**：[libusb-win32](https://github.com/mcuee/libusb-win32) 官方发布包 `v1.4.0.0`（`libusb0.sys` / `libusb0.dll`）
- **开源许可**：`wdi-simple.exe`（`libwdi`）遵循 **LGPL-3.0**（见同目录 [`LICENSE-LGPL-3.0.txt`](LICENSE-LGPL-3.0.txt)）；内嵌 `libusb-win32` 包含 GPL-3.0 / LGPL-3.0 组件。本文件作为独立 CLI 可执行文件被上层 MIT Python 脚本通过命令行调用。
- **构建方式**（MSYS2 MinGW64，静态链接，无外部 MinGW DLL 运行时依赖）：
  ```bash
  ./bootstrap.sh
  CFLAGS="-O2" LDFLAGS="-static -static-libgcc" ./configure \
    --with-libusb0=<libusb-win32-bin-1.4.0.0> \
    --enable-examples --disable-debug --disable-shared --enable-static --disable-32bit
  make -C libwdi -j4 && make -C examples wdi-simple.exe -j4
  ```
- **产物校验**：
  - 大小：1560339 bytes
  - SHA-256：`53d32227ae79b757476f628e0703583df1c720ecb0216e7dd295a7ea496f91e8`
