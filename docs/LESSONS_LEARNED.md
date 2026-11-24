# 实机调试踩坑记录与根因分析 (Lessons Learned)

本文档完整记录在 Sony ILCE-7RM3 (v3.10) 真机纯 RAM 调试过程中遇到的所有隐蔽陷阱、现象定位过程及最终验证的解决方案，避免后续开发重蹈覆辙。

---

## 1. 显存与界面绘制篇

### 1.1 不带 `O_SYNC` 映射 `/dev/mem` 导致缺块与雪花
- **现象**：直接向映射出的物理显存写像素后，屏幕出现横向断裂、花屏或刷新滞后。
- **根因**：ARM CPU 的 L1/L2 Cache 会缓存普通 `mmap(/dev/mem)` 的写操作，显存控制器绕过 CPU Cache 直接扫描物理 RAM。
- **解决**：必须以 **`O_RDWR | O_SYNC` (`0x101002`)** 打开 `/dev/mem`，且每次批量写完执行 `dsb` 指令。

### 1.2 `render` (`0x202E0200`) 与 `flipA/B` 排列完全不同
- **现象**：按线性行优先写入 `0x202E0200` 画面严重错位成马赛克且上下颠倒。
- **根因**：`0x202E0200` 是 GPU 渲染目标，采用 **8×8 Morton Z-order 分块 + 垂直上下翻转 (`yy = 479 - y`)**；而 `0x23A80000`/`0x23B16000` 为常规线性行优先。
- **解决**：实现 `a7r3_morton_index(x, y)` 预打包函数，一次性生成线性与 Morton 两份缓存后再突发拷贝。

### 1.3 原生图标穿透与闪烁
- **现象**：打开拓展菜单后，几百毫秒内屏幕上原厂电池/图标或半透明灰条又闪回覆盖自定义菜单。
- **根因**：只检测第 0/1 列的简单采样会漏掉窄横条；同时 `im.elf` 内部在 GPU `ioctl` 前后仍会局部提交绘制。
- **解决**：改为 60 条带 **互质步长抽检 (`BAND_STEP = 79`, 与行宽 320 互质)** + 每 5 ms 轮询 2 个全带全量比对 + 原子挂钩 `libObj.so` `ioctl` GOT (`0x0121cad0`) 在每次提交前后立即自愈。

---

## 2. 链接器与纯 RAM 注入篇

### 2.1 `DT_RELCOUNT` 导致 glibc 2.11.2 `ld.so` 当场崩溃
- **现象**：使用新版 `ld.lld` 默认参数链接出的 `.so` 在机内 `dlopen(..., RTLD_NOW)` 时立即触发段错误/崩溃。
- **根因**：`ld.lld` 默认启用 `-z combreloc` 输出 `DT_RELCOUNT`，与旧版 glibc 2.11.2 `ld.so` 的 `BIND_NOW` 重定位路径不兼容。
- **解决**：链接必须强制传入 **`-Wl,-z,nocombreloc`**，并在 `build_addon.py` 中将其列为硬性编译检查项。

### 2.2 `libObj.so` 数据段与文件偏移存在 `+0x8000` 差值
- **现象**：根据静态文件偏移计算出的 `ObjStillRecorder::ParseRecCntInfoPinCommand` 虚表槽位指向错误地址，挂钩无触发甚至破坏邻近数据。
- **根因**：`libObj.so` 的 `PT_LOAD[1]`（RW 段，包含虚表与 GOT）在运行时的虚拟地址相较文件偏移存在 **`+0x8000`** 对齐间隙（文件偏移 `0x011fe15c` 实际位于 `libObj + 0x0120615c`）。
- **解决**：所有 RW 槽位必须按 `PT_LOAD` 的 `p_vaddr`（如 `0x0120615c`）而非裸文件偏移计算。

### 2.3 `DL_RC_25` 报错与禁用 `dlclose()`
- **现象**：当进程中仍有被挂钩的线程存在，若贸然反复重试或执行 `dlclose()` 会导致 `DL_RC_25` 或直接死锁崩溃。
- **解决**：严禁 `dlclose()`；热更新时先安全 `addon_quit` 还原全部 GOT/vptr 并确认工作线程退出，再以单调递增新文件名 `/tmp_bt/addon_ram_N.so` 加载。

---

## 3. 相册对焦点功能篇

### 3.1 连拍组内/组外、DISP 直方图与 MENU 冲突
- **现象**：浏览连拍分组封面时残存上一张照片的红框；按 DISP 切到直方图或按 MENU 键时红框仍漂浮在菜单上；缩放回缩略图界面红框不消失。
- **解决**：结合 `ViewSinglePb` 生命周期钩子、`GetDispType` GOT（`0x008cc9d4`）、`VU2_GROUP_PB_STAT_OFF` / `VU2_CONT_PB_GRP_OFF`、以及 `a7r3_input` 中的 MENU 键活动序列号实时控制显隐与复原。

---

## 4. 电子快门自定义音效篇（重点排查实录）

### 4.1 静音拍摄参数枚举反直觉
- **现象**：按常规逻辑以为 `1 = ON, 0 = OFF` 导致在电子快门模式下始终判定为机械快门而跳过发声。
- **实机验证**：`param 0x293` / Backup ID `0x01070b2c` 实际采用 **`2 = ON（静音拍摄开），1 = OFF（静音拍摄关）`**。

### 4.2 `AdfCh::SysSetPath(1)` 导致“调用成功但无声”
- **现象**：音频测试试听无报错且返回成功，但机身扬声器毫无声音；并且 `SysRequest` 的参数顺序和固件预期不符。
- **根因**：`libObj.so` 内部音频通路查表值为 `{3, 0, 2}`，**`path = 3` 才是有效的机身扬声器通路**，而 `path = 1` 代表静音/空闲态！同时 `SysRequest` 正确参数序为 `(self, req=1, reserve=0, kind_id, loop=0)`。

### 4.3 SD 卡 `/SHUTTER_SFX/*.wav` 无法识别 (`SDK_SFX_SD_CNT=0`)
- **现象**：`Fsys_FindFirst` / `Fsys_Open` 始终返回失败。
- **根因**：
  1. `libInfraFsys.so` 高层接口内部会调用 `fsys_CreatePathInfo`（`0x1cff8`），该函数严格限制 **8.3 短文件名**；`SHUTTER_SFX` 共 11 个字符被直接以 `0x80000041` 拒绝。
  2. 原型阶段预留的 `findInfo` 缓冲区仅 `2080` 字节，但实机反汇编显示 `sizeof(findInfo) == 0x828`（`2088` 字节），过小导致越界/失败。
- **解决**：绕过 `fsys_CreatePathInfo`，通过 `fsys_GetDriveInfoFromPath` 锁定设备后直接调用底层 `PrFILE_fsfirst` / `PrFILE_fsnext`（缓冲区扩大至 **`2112 B`**）及 `pf2_fopen` / `pf2_fread` / `pf2_fclose`（或 TNTFS GOT），并及时调用 `fsysDep_UnlockDevice()` 解锁。

### 4.4 用户 `.wav` 文件实际为 MP3 封装 (`SDK_SFX_WAV_HDR=0x550100bb`)
- **现象**：SD 卡文件能扫出来，但加载时被 WAV 解析器拒绝，日志打印 `audio_fmt = 0x55`。
- **根因**：部分网上下载的 `.wav` 音效文件实为 RIFF 容器内封装的 MPEG Layer-3（`0x0055`），并非未压缩 PCM。
- **解决**：扩展机内解析器支持 8/16/24-bit PCM 以及 `WAVE_FORMAT_EXTENSIBLE (0xFFFE)`，并在文档明确引导用户用 `ffmpeg` 转为 `pcm_s16le` 24 kHz 单声道 WAV。

### 4.5 原生 UI 严重卡死（按 MENU 1s+、连拍缓存条/写卡灯/EVF/相册全部迟滞）
- **现象**：开启模块后，相机除了能正常对焦拍摄外，整个原厂 UI 极其卡顿：MENU 响应延迟超过 1 秒，连拍缓存进度条卡死，写卡指示灯不亮，遮挡 EVF 不切换，相册难以打开。
- **根因分析**：`a7r3_shutter_sfx.c` 此前为了实时读取静音状态，在后台工作线程调用了 `viewUnified2.so` 的 `CmnViewModelIfWrapper::getModelInfo`（`0x2ce4fe -> 0x38ccf8`）。该函数内部会读写属于 **`Appframework` 主线程 (`tid=392`) 独享的非线程安全 `CmnMRUtil` `std::map` 容器**，跨线程并发访问直接破坏了其内部状态/锁结构，导致 `Appframework` UI 调度线程几乎停止工作。
- **根治方法**：**彻底移除 `a7r3_shutter_sfx.c` 对 `viewUnified2.so` 的任何调用**，改由 `libObj.so` 导出的线程安全持久化读取接口 `Backup_read(0x01070b2c)` 直接查询静音拍摄状态。实测日志稳定输出 `SDK_SFX_ESHUT_VAL=0x2`、`SDK_SFX_ESHUT=0x1`，原生 UI 恢复原厂流畅度。

### 4.6 电子快门发声延迟 ~200 ms 与连拍拥堵
- **现象**：最初版本工作线程采用 5 ms 轮询睡眠，且每次触发先后同步调用 `AdfCh::SysSetPath` 和 `AdfCh::SysRequest`，连拍时声画明显脱节。
- **实测打点 (`SDK_SFX_TIMING=`)**：`AdfCh::SysSetPath` 单次耗时可达 `37~91 ms`，而同步 `AdfCh::SysRequest` 高达 **`186~205 ms`**。
- **解决**：
  1. 用 ARM Linux **`futex` (`SYS_FUTEX=240`)** 替代 5 ms 轮询，实现从快门回调到音频线程唤醒的零空转、微秒级响应；
  2. 在 `sfx_worker` 中持久维护一个 `AdfCh(0x2b)` 实例，消除反复构造/析构 IPC 通道开销；
  3. 改用底层 **`DataflowInfraCh::sndAsyncDataflowMsg` (`libBizFw + 0x00033a29`)** 异步“发后即忘”方式向 `ADF_DEST_ID (0x0058003f)` 投递 `0x0202`/`0x0203` 消息，投递延迟降至 `< 1 ms`，彻底解决连拍快门声拥堵。
