# Sony ILCE-7RM3 (A7R3 v3.10) 互操作性地址偏移与协议笔记

> **合规声明**：本文档仅为纯内存软件互操作性研究与二次开发整理的**内存偏移笔记与协议签名**，不含任何源自原厂固件的二进制代码、镜像或受版权保护的静态资源。所列参数已在 **ILCE-7RM3 / 固件 v3.10** 上实测校验，切勿套用于其他固件版本或未经验证的机型。

---

## 1. 系统基础库 (`libc` / `libdl`) 与物理显存

| 模块 / 资源 | 符号 / 角色 | 偏移 / 物理地址 | 说明 |
| --- | --- | --- | --- |
| `/lib/libc-2.11.2.so` | SVC 系统调用跳板 | `+0x00024F78` | 远程 `ptrace` 执行 `mmap2`/`munmap`/`cacheflush` |
| `/lib/libdl-2.11.2.so` | `dlopen` (Thumb) | `+0x00000BC9` | 远程加载 `/tmp_bt/addon_ram*.so` |
| `/lib/libdl-2.11.2.so` | `dlerror` (Thumb) | `+0x00000E05` | 诊断加载失败原因 |
| 物理内存 `/dev/mem` | `A7R3_PA_RENDER` | `0x202E0200` | `640×480 RGBA4444`，8×8 Morton Z-order，Y 轴翻转 |
| 物理内存 `/dev/mem` | `A7R3_PA_FLIP_A` | `0x23A80000` | `640×480 RGBA4444`，线性行优先，正向 |
| 物理内存 `/dev/mem` | `A7R3_PA_FLIP_B` | `0x23B16000` | `640×480 RGBA4444`，线性行优先，正向 |

> `/dev/mem` 必须以 `O_RDWR | O_SYNC`（ARM Linux `0x101002`）打开；系统调用号 `SYS_FUTEX = 240`，`FUTEX_WAIT_PRIV = 128`，`FUTEX_WAKE_PRIV = 129`。

---

## 2. `libObj.so` 偏移表

> **重要提醒**：`libObj.so` 的第二个 `PT_LOAD`（RW 数据段，涵盖虚表和 `.got`）在内存中的虚拟地址比文件偏移多 **`+0x8000`**（例如虚表文件偏移 `0x011fe15c` 对应的运行时内存偏移为 `0x0120615c`）。下表均为**运行时相对于 `libObj.so` 基址的偏移**：

| 符号 / 用途 | 类型 | 相对基址偏移 | 来源模块 |
| --- | --- | --- | --- |
| `ioctl` | GOT 槽 | `0x0121cad0` | `a7r3_fb.h` |
| `osal_rcv_msg_tmo` | GOT 槽 | `0x0121c8c0` | `a7r3_input.c` |
| `osal_free_msg` | GOT 槽 | `0x0121c7a8` | `a7r3_input.c` |
| `osal_valloc` | GOT 槽 | `0x0121c35c` | `a7r3_input.c` |
| `osal_snd_msg` | GOT 槽 | `0x0121ca94` | `a7r3_input.c` |
| `InfraInput::GetPhy` | GOT / Real (Thumb) | GOT `0x0121c990` / Real `0x0084b701` | `a7r3_input.c` |
| `InfraInput::GetEv` | GOT / Real (Thumb) | GOT `0x0121ac2c` / Real `0x0084c765` | `a7r3_input.c` |
| `s_InfraTp` 指针 | 全局指针 | `0x012d4ebc` | `a7r3_input.c` |
| `s_InfraInput` 指针 | 全局指针 | `0x012d4ee8` | `a7r3_input.c` |
| `TextSprite::SetString(res_id)` | GOT / Real (Thumb) | GOT `0x0121aa9c` / Real `0x005dbe09` | `a7r3_entry_cert.c` |
| `TextSprite::SetString(utf8)` | Real (Thumb) | `0x005dbfa1` | `a7r3_entry_cert.c` |
| `getResID` | GOT / Real (Thumb) | GOT `0x0121a5c0` / Real `0x00798d31` | `a7r3_dual_hud.c` |
| `RecDtor` | GOT / Real (Thumb) | GOT `0x0121a51c` / Real `0x00798d23` | `a7r3_dual_hud.c` |
| `ResourceLayoutParser` 调用返回点 | Text 返回地址 | `0x0059feca` | `a7r3_dual_hud.c` |
| `PlayStillSingle` | GOT | `0x0121b818` | `a7r3_pb_focus.c` |
| `ObjStillRecorder::ParseRecCntInfoPinCommand` | vtable 槽 / Real (Thumb) | Slot `0x0120615c` / Real `0x008640fd` | `a7r3_shutter_sfx.c` |
| `MWF::ObjMsg::GetMessageId` | GOT | `0x0121c040` | `a7r3_shutter_sfx.c` |
| `MWF::ObjMsg::GetParam` | GOT | `0x0121b470` | `a7r3_shutter_sfx.c` |
| `Backup_read` | GOT | `0x0121c0c0` | `a7r3_shutter_sfx.c` |

---

## 3. `viewUnified2.so` 与 `libmpr.so` 偏移表

### `viewUnified2.so`

| 符号 / 状态变量 | 类型 | 相对基址偏移 | 备注 |
| --- | --- | --- | --- |
| `CmnViewSettingNode` 活动菜单树根 | 全局对象 (`40 B`) | `0x00a51c60` | 原生 `count = 6`，可扩展为 `7`（`a7r3_native_tab7.c`） |
| `PAS_MenuListTab` 更新虚表槽 | vtable `+0x1ac` / Real | `0x008bcb7c` / `0x0056ef3d` | 后置摆放第 7 大类控件（`m_tabCount @ +0x1c8`, 图标 `+0x1840`, 焦条 `+0x2720`） |
| `PAS_MenuListTab` 控件布局函数 (Thumb) | 内部函数 | `0x0056d71b` | `(self, widget, int16 rect[4], visible)` |
| `CmnViewProcessDataMgr` guard / inst / vptr | 全局单例 | `0x00a48b0c` / `0x00a48b10` / `0x00867ae0` | 槽位 `getValue(+0x0c)`, `isGray(+0x24/+0x2c)`, `isDis(+0x34/+0x38)`, `setVal(+0x44)`, `exec(+0x54)` |
| `CmnViewProcessDataMgr` guard / inst | 全局指针 | `0x00a48b0c` / `0x00a48b10` | `a7r3_entry_cert.c` |
| `CertificationLogo` getter (Thumb) | 函数 | `0x003ec7b9` | `proc_id = 0x10f` |
| `CertificationLogo` guard / inst | 全局指针 | `0x00a6f380` / `0x00a6f384` | `a7r3_entry_cert.c` |
| `CertificationLogo` 原 vtable | `.data.rel.ro` | `0x0088d850` | 槽位：`GRAY=+0x38`, `DIS=+0x48`, `EXEC=+0x68` |
| 原生认证标识文本资源 ID | 资源常量 | `0x0204ecba` / `eb` / `ec` | 标题 / 提示 / 确认弹窗文案 |
| `UserGetLayoutMode` | GOT / Real / Gate1 返回点 | `0x008cd0fc` / `0x0029c411` / `0x001ae78c` | `a7r3_dual_hud.c` |
| `Level` 视图路由虚表槽 | Slot / Real (Thumb) | `0x008602d4` / `0x0017a0b1` | 控件 ID `0xc96d8ddc` -> `0xd1153927` |
| `RES_ID_LEVEL_OUTER` | 布局资源 ID | `0x02008029` | 外层水平仪布局 |
| `ViewSinglePb` vtable `DTOR1` / `DTOR2` | Slot / Real | `0x00861990` (`0x1ead39`) / `0x00861994` (`0x1ead81`) | `a7r3_pb_focus.c` |
| `ViewSinglePb` vtable `OPEN` / `CLOSE` | Slot / Real | `0x00861a40` (`0x1f5a7b`) / `0x00861a44` (`0x1f5b05`) | `a7r3_pb_focus.c` |
| `ViewSinglePb` vtable `EXIT` | Slot / Real | `0x00861a54` (`0x1f79bd`) | `a7r3_pb_focus.c` |
| `GetDispType` | GOT / Real (Thumb) | `0x008cc9d4` / `0x003cdc45` | `a7r3_pb_focus.c` |
| 回放状态字节 | 全局标志 | `CUR_PB_BASE=0x00a6cd2c`, `ENLARGE=0x00a6cd36`, `MOVIE=0x00a6cd53`, `GROUP_STAT=0x00a6e1e8`, `CONT_GRP=0x00a6e1e9` | `a7r3_pb_focus.c` |

### `libmpr.so`

| 符号 | 类型 | 相对基址偏移 | 备注 |
| --- | --- | --- | --- |
| `&(s_FeaCore)` 字面量指针 | 指针 | `0x00874b4c` | `a7r3_pb_focus.c` |
| `FeaCore` 原 vptr | vtable | `0x00a0af20` | 拷贝 14 字，替换槽 +7 (`+0x1c`) |
| `FeaCore::SendCallback` (Thumb) | Real | `0x0050c675` | 拦截 `0x40000022` / `0x40000085` |
| `PlayStillSingle` (Thumb) | Real | `0x00881889` | 提取回放 EXIF 缓冲区 |

---

## 4. `libBizFw.so`（音频 `AdfCh` / 异步数据流 / `DmmCh` / 物理内存）

| 类 / 接口 | 偏移 (Thumb) | 常量 / 备注 |
| --- | --- | --- |
| `DataflowInfraCh::sndAsyncDataflowMsg` | `0x00033a29` | 异步消息投递，无 200ms 阻塞 |
| `AdfCh` ctor / dtor | `0x0003a9d1` / `0x0003a399` | `ADF_UNIT_ID = 0x2b`, `ADF_DEST_ID = 0x0058003f` |
| `AdfCh::SysSetTable` | `0x0003b29d` | `APP_UNIT_TYPE = 0x00070817`, `CUSTOM_KIND_ID = 9` |
| `AdfCh::SysSetPath` | `0x0003b3f5` | `path = 3` 为机身扬声器有效通路（`1` 会静音） |
| `AdfCh::SysRequest` | `0x0003b509` | 同步接口（耗时 ~186–205 ms，仅作参考） |
| `AdfCh::SysSetVolume` | `0x0003b62d` | 调节音量 |
| `DmmCh` ctor / dtor / alloc / open / close / free | `0x35ba9` / `0x35b55` / `0x35c19` / `0x35f31` / `0x36039` / `0x36131` | 物理 DMA 音频缓冲句柄管理 |
| `AvbbcPhycMem` ctor / getlog / dtor | `0x0003179d` / `0x000315f5` / `0x00031715` | 物理地址映射到进程虚拟地址 |

### 异步 ADF IPC 消息结构（用于 `sndAsyncDataflowMsg`）

```c
// 设置音频通路消息 (size = 12)
struct adf_setpath_msg {
    uint16_t magic;  /* 0x2004 */
    uint16_t cmd_id; /* 0x0202 */
    uint16_t size;   /* 12 */
    uint16_t ver;    /* 1 */
    uint8_t  path;   /* 3 = 扬声器 */
    uint8_t  pad[3];
};

// 异步触发播放消息 (size = 16)
struct adf_sysreq_msg {
    uint16_t magic;   /* 0x2004 */
    uint16_t cmd_id;  /* 0x0203 */
    uint16_t size;    /* 16 */
    uint16_t ver;     /* 1 */
    uint8_t  req;     /* 1 = START */
    uint8_t  reserve; /* 0 */
    uint8_t  kind_id; /* 内置 0..8 或自定义 9 */
    uint8_t  pad;
    uint32_t loop;    /* 0 = 单次播放 */
};
```

> **Sony ADF 32 字节 BFP 块格式**：采样率 `24000 Hz` 单声道；每块 32 字节描述 31 个采样点——`byte[0]` 低 4 位为左移指数 `lo ∈ [0..8]`（高 4 位 `0` 表示直接 BFP PCM 帧），`byte[1..31]` 为 31 个有符号 `int8_t` 量化值 `q ∈ [-127..127]`；总长度按 64 字节（偶数个块）对齐，最大注册缓冲 `32768` 字节（约 `0.68 s`）。

---

## 5. `libInfraFsys.so`（SD 卡 PrFILE2 LFN 与 TNTFS）

| 符号 | 偏移 / GOT | 作用 |
| --- | --- | --- |
| `Fsys_GetMountDrives` | `0x00017d71` | 枚举已挂载盘符位掩码 |
| `fsys_GetDriveInfoFromPath` | `0x00016af1` | 获取指定盘符 `drv` 对象 |
| `fsys_GetDeviceMutexId` / `fsysDep_UnlockDevice` | `0x00016a11` / `0x00024f09` | 加解锁设备互斥量（`drv + 0x954`） |
| `fsysDep_SetContext` | `0x0002501d` | 绑定线程文件系统上下文 |
| `PrFILE_fsfirst` / `PrFILE_fsnext` | `0x0002033d` / `0x00020531` | 支持长文件名目录枚举（绕过 8.3 限制，`findInfo` 需 `>= 0x828` 字节） |
| `pf2_fopen` / `pf2_fread` / `pf2_fclose` | `0x0002cb85` / `0x0002cc31` / `0x0002c9f9` | 长文件名文件读取 |
| `mcmn_drv_tntfs_fopen/fread/fclose` GOT | `0x0006b698` / `0x0006b8b8` / `0x0006b7e4` | TNTFS 驱动兜底读取 |

---

## 6. Backup 参数 ID 与 OSAL 输入按键码

### Backup IDs（经 `libObj Backup_read` 线程安全读取）

| Backup ID | 含义 | 值域 |
| --- | --- | --- |
| `0x01070b2c` | 静音拍摄（电子快门） | **`2 = 开启（ON）`，`1 = 关闭（OFF）`** |
| `0x002b0022` | 对焦锁音音量 (`BEEP_ID_FORCUSLOCK`) | 参考 `a7r3_shutter_sfx.c` |
| `0x002b002a` | 电子快门提示音音量 (`BEEP_ID_KASHA`) | 参考 `a7r3_shutter_sfx.c` |

### OSAL 队列与物理键码 (`a7r3_input.h`)

- `QID_INPUT_MAIN = 0x007a0020`（实体按键/传感器事件）
- `QID_TP_MAIN = 0x007b0026`（电容触屏原始坐标）

| 常量 | 键值 | 说明 |
| --- | --- | --- |
| `A7R3_KEY_UP` / `DOWN` / `LEFT` / `RIGHT` | `0x003` / `0x030` / `0x015` / `0x1af` | 十字方向键 |
| `A7R3_KEY_ENTER` / `MENU` | `0x1ac` / `0x1ab` | 中央确认键 / MENU 键 |
| `A7R3_KEY_EV_DIAL1` / `EV_DIAL2` | `0x70d2` / `0x70d3` | 物理曝光补偿拨盘 |
| `A7R3_KEY_EYE_CONTACT` / `FINDER_LCD` | `0x708b` / `0x708f` | EVF 眼传感器 / 取景器屏幕切换 |
| `A7R3_KEY_EVF_OPEN` / `CLOSE` / `PANEL_TILT` | `0x01d5` / `0x01d6` / `0x70ef` | 取景器与翻转屏状态事件 |
