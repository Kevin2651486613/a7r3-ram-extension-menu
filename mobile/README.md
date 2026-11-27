# 手机注入（Android + Termux，免 root）

在**没有电脑**的情况下，用一台 Android 手机通过 OTG 数据线把
`prebuilt/demo_native_tab7_ram.so` 纯内存注入到相机的 `im.elf`。

和桌面端一样：**零 Flash 写入**，所有文件只落在相机的 `/tmp_bt/`（tmpfs），
相机断电重启即恢复原厂状态。

> 仅在 **ILCE-7RM3 固件 v3.10** 上验证过。风险与免责声明见仓库根目录
> [`DISCLAIMER.md`](../DISCLAIMER.md)，手机方案不改变这些前提。

---

## 一、为什么可行

整个注入链路对 USB 的要求其实很窄：

| 环节 | 用到的 USB 原语 |
| --- | --- |
| 切 Service Mode | 1 个 vendor 控制传输 + 3 轮 bulk 挑战应答 |
| 相机 shell | bulk 端点读写（Senser terminal） |
| 上传 + dlopen | bulk 分段传输 |

没有任何内核驱动依赖，所以只要能让用户态拿到"原始 USB 端点"，手机就能做。
Android 上这件事由 `termux-usb` 完成：它通过 `UsbManager` 打开设备，把
**文件描述符**交给子进程，再用 `libusb_wrap_sys_device()` 包成 libusb 句柄。

三个关键适配点：

1. **抢接口**：相机在 Mass Storage 模式下被内核 `usb-storage` 占用，
   必须先 `detachKernelDriver(0)` 再 `claimInterface(0)`。
   实测多数机型允许（不需要 root），少数机型会被 SELinux 拒绝。
2. **ARM 机器码**：部署用的 ELF 依赖运行时地址（`libc_base`、`mmap` 返回值），
   无法预生成。桌面端用 keystone 现场汇编，而 Termux 是 aarch64、没有对应轮子，
   因此 `termux/keystone.py` 把同一接口转发给 **llvm-mc + ld.lld + llvm-objcopy**。
3. **复用部署逻辑**：`m2_deploy.py` 直接调用仓库原有的
   `tools/deploy_ram.py:cmd_deploy()`，只替换最底层的 USB 驱动，
   部署顺序、PID 校验、路径白名单、钩子检查全部沿用桌面实现。

---

## 二、前置条件

- Android 手机（支持 OTG 主机模式）
- [Termux](https://f-droid.org/packages/com.termux/)（F-Droid 版）
- **Termux:API** 应用（同一个 F-Droid 仓库，必须装，否则 `termux-usb` 不可用）
- OTG 数据线（能传数据，不是纯充电线）
- 相机：`MENU -> 设置(黄箱子) -> USB连接` 设为 **大容量存储 (Mass Storage)**

---

## 三、安装

```bash
pkg install -y git
git clone https://github.com/Kevin2651486613/a7r3-ram-extension-menu
cd a7r3-ram-extension-menu
bash mobile/install.sh
```

`install.sh` 会依次完成：安装 `python / termux-api / libusb / lld / llvm`、
`pip install libusb1`、运行汇编器自检、并把 `inject` 注册到 `$PREFIX/bin`。

自检必须输出 `ALL_OK`，否则不要继续：

```
case 0 OK
case 1 OK
case 2 OK
ALL_OK
```

---

## 四、使用

相机开机、设为 Mass Storage、插上 OTG 线，然后只敲一条命令：

```bash
inject
```

它会自动：

1. 扫描 `termux-usb -l` 列出的设备；
2. 若只有一个设备 → 执行 `m1_switch.py` 切到 Service Mode；
3. 轮询等待相机重新枚举为 `054C:0336`（最多 25 秒）；
4. 对新设备执行 `m2_deploy.py` 完成纯 RAM 部署；
5. 打印 `[OK]`。

若相机已在服务模式（列出 2 个设备），会跳过第 2 步直接部署，
所以中途失败后重敲一次 `inject` 即可续跑。

### 成功标志

```
STG1 pid 0x0336
STG3 device ok
  probe_out 'PING\n'
STG5 TERMINAL_READY
  pid_im '158\n'
STG6 so 117592
[+] dlopen(/tmp_bt/addon_ram.so) -> handle=0x...
DEPLOY_RC 0
```

看到 `DEPLOY_RC 0` 后拔线，在相机上按 `MENU`，顶栏右侧即出现第 7 大类「拓展菜单」。

---

## 五、故障排查

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| `termux-usb: not installed` | 缺 `termux-api` 包或缺 Termux:API 应用 | `pkg install termux-api` 并安装 Termux:API APK |
| `termux-usb -l` 为空 | 未授权 / OTG 线不支持数据 / 相机不在 MSC | 重新插拔，确认线材与 USB 模式 |
| `claim_err ... BUSY` | 内核 `usb-storage` 不允许被顶掉 | 换机型或使用 root；见下文限制 |
| `WRONG_DEVICE` | 对 MSC 设备跑了部署步骤 | 先执行切换步骤，或直接 `inject` |
| `LIBUSB_ERROR_NO_DEVICE` | 设备已重新枚举，fd 失效 | 重新插拔相机后重跑 `inject` |
| `LIBUSB_ERROR_PIPE` | 端点 STALL 后未恢复 | 脚本已内置 `clearHalt` 重试；若仍失败请重新插拔 |
| `missing tool: ld.lld` | 未装 lld | `pkg install lld` |
| `case 2 MISMATCH` | 链接器没有应用加载地址 | 确认 `lld` 版本，或提交 issue 附 `exp`/`got` |
| 结尾 `AssertionError ... __finalize__` | python-libusb1 退出回收的已知噪音 | 可忽略，不影响结果 |

---

## 六、已知限制

1. **每次开机都要重新注入**。内存驻留方案，关机即失效，与桌面端一致。
2. **首次会弹 USB 授权框**。MSC 与切换后的 0336 各弹一次，这是 Android 的机制，
   无法绕过，点「允许」即可。
3. **免 root 依赖内核允许 detach**。已实测通过的机型可以正常工作；
   若你的机器报 `BUSY`，说明该机型 SELinux 不允许抢占已被 `usb-storage` 绑定的接口，
   此时只能使用 root、或改用电脑 / 树莓派方案。
4. **不支持 iPhone**。iOS 不向第三方应用开放原始 USB Host 访问。

---

## 七、安全边界

手机方案没有放宽任何一条原有约束：

- 切换步骤只发 1 个 vendor 控制传输 + 认证读写，**不写相机**；
- 部署步骤全部文件落在 `/tmp_bt/`，由 `deploy_ram.py` 校验其必须为 tmpfs；
- 机型 / PID / 固件库映射 / ELF 边界校验全部沿用桌面实现，失败即中止；
- 注入中途失败不会留下任何持久化痕迹，断电重启即恢复。

---

## 八、目录结构

```text
mobile/
├── README.md          # 本文档
├── install.sh         # Termux 一键安装（依赖 + 自检 + inject 命令）
└── termux/
    ├── inject.py      # 单命令编排：扫描 -> 切模式 -> 等枚举 -> 部署
    ├── m1_switch.py   # 步骤 1：Mass Storage -> Service Mode
    ├── m2_deploy.py   # 步骤 2：纯 RAM 部署（调用 deploy_ram.cmd_deploy）
    ├── keystone.py    # keystone 替身：转发给 LLVM 工具链
    └── selftest.py    # 汇编器逐字节自检
```

---

## 九、许可

与仓库主体一致，MIT。手机方案没有引入任何额外的第三方二进制，
仅依赖 Termux 软件源中的 `llvm` / `lld` / `libusb`。
