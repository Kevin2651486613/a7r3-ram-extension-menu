# 给 Agent 的执行提示词

把下面「提示词正文」整段复制给 Agent 即可。Agent 应在仓库根目录工作。

---

## 提示词正文

你要在本仓库（Sony α7R III / ILCE-7RM3 固件 v3.10 纯 RAM 拓展菜单工具）里完成环境准备和离线自检。按顺序执行，每一步都要看到真实输出再进行下一步。

**第 1 步：确认代码版本**
- 运行 `git status` 和 `git log --oneline -3`，记录当前提交号。
- 如果工作区有未提交改动，先告诉用户，不要丢弃或覆盖。

**第 2 步：准备 Python 环境**
- 要求 Python 3.10 或更高。运行 `python --version`，不满足就停下并报告。
- 在仓库根目录执行：`python -m venv .venv`，然后激活它，再执行 `pip install -r requirements.txt`。
- 精简版 MIT `pmca` 通信层已内置于 `tools/vendor/pmca/`，**无需**再单独克隆 `Sony-PMCA-RE`。

**第 3 步：配置本机路径**
- 把 `.env.example` 复制为 `.env.local`（该文件已被 gitignore）。
- 填写 `A7R3_LLVM_BIN`：指向含 `clang`、`ld.lld`、`llvm-readelf` 的目录（需要 LLVM 16 或更高）。
- `A7R3_FW_LIB_DIR` 和 `A7R3_UXC_PATH` 暂时可以留空，它们需要相机数据，要等第 5 步之后。
- 不要把 `.env.local` 的内容提交或输出到日志。

**第 4 步：离线自检和编译**
- 运行 `python tools/a7r3_addon_console.py check`。所有项必须是 `[OK]`。有任何 `[FAIL]` 或报错，停下，把完整输出报告给用户，不要自行绕过。
- 运行 `python tools/a7r3_addon_console.py build`，记录结果和退出码。

**第 5 步：需要相机时必须先问用户**
- 下面这些操作会连接相机，并让相机进入 Service Mode，必须先得到用户明确同意：`init-camera`、`deploy`、`open`。
- 得到同意后，才能运行 `init-camera`。它只读取固件文件并做 MD5 校验，不写入相机和闪存。成功后它会把库保存到 `./firmware/lib`，之后再把 `A7R3_FW_LIB_DIR` 指向这个目录。
- 相机型号读取失败时，不要自行加 `--force`。先报告，等用户决定。

**硬性规则**
- 不要执行 `git commit`、`git push`，除非用户明确要求。
- 不要修改 `firmware/` 目录下的文件，也不要把它加入版本控制。
- 不要修改 `patch_viewstlrec` 里的指纹校验，也不要添加绕过开关。
- 任何一步失败，都停止后续步骤，报告失败的命令、完整输出和退出码。

**最终报告格式**
1. 当前提交号和工作区状态。
2. 每一步的结果：通过 / 失败 / 跳过，并附关键输出。
3. 哪些是离线验证的，哪些是硬件验证的。没有连接相机，就明确写「硬件未测试」。
4. 需要用户决定的事项。

---

## 常用快捷入口（给用户与维护者）
- Windows 用户无需手动运行 Zadig：将相机设为 **大容量存储 (MSC)** 连线后，直接双击 `一键注入拓展菜单.bat`（自动提权 → 自动绑定临时 `libusb-win32` → 如缺失则自动只读初始化并编译 → 纯 RAM 注入 → 自动卸载全部临时驱动并恢复原生 `USBSTOR`）。
- 命令行等效指令：`python tools/a7r3_addon_console.py one-click`；手动清理恢复驱动：`python tools/a7r3_addon_console.py restore-driver`。
- `.env.local`、`firmware/` 与 `experiments/` 都是每台机器自己准备的，不随仓库分发；USB 通信层已内置于 `tools/vendor/pmca/`。
