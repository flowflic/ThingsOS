# Metalio USB 固件烧录与恢复流程

适用项目：`D:\ThingsOS_nologotech`

适用设备：Metalio E-Ink4-Plus，ESP32-S31，16 MiB Flash

实测日期：2026-10-31

成功记录：恢复 0.1.0 正式主应用、在正式版基础上安装 0.1.0 键盘修复候选。

本文记录已经成功的连接与安装路径。它不是新的烧录授权，也不代表候选功能已完成
实机验收。后续每次安装仍需用户明确授权，常规修复保持原版本号。

## 1. 成功路径概览

```text
核对设备、布局、候选与哈希
  → 确认当前主应用/救援状态
  → 同一串口会话快速进入 ROM、同步并处理 S31 下载看门狗
  → 加载 Espressif stub
  → 完整读取并保存 16 MiB 备份
  → 选择并实际启动救援，静置并捕获 45 秒
  → 再次快速连接 ROM/stub，记录救援启动后的 NVS
  → 仅写主应用 @0x220000
  → 完整回读 16 MiB，核对镜像及保护区域
  → 校验通过后才写启动选择数据 @0xf000，并回读核对
  → 启动新主应用，捕获 45 秒日志，确认 ELF、书库和启动健康
  → 关闭串口，交给用户测试功能/显示效果
```

若设备已经因主应用损坏而运行救援，先验证救援确实运行并静置 45 秒，再进入 ROM
备份和恢复；无需为了进入救援再次改写启动选择数据。不能直接套用这一分支更新
仍在正常运行的主应用。

## 2. 环境、文件和分区

本次成功环境：Windows PowerShell、ESP-IDF v6.1 配套 Python、esptool 5.5.0、
pySerial。以下路径为本机实测值，不保证其他电脑相同：

```powershell
$usbFlashPython = 'D:\crossmux\.tools\idf-tools\python_env\idf6.1_py3.12_env\Scripts\python.exe'
& $usbFlashPython -m esptool version
Get-Content -LiteralPath D:\ThingsOS_nologotech\native\VERSION
```

| 对象 | 本次值 / 要求 |
| --- | --- |
| USB 串口 | 本次 COM5；每次按设备 USB 序列号重新查找，不假定一直是 COM5 |
| USB 标识 | VID:PID `303a:1001`，USB-Serial/JTAG |
| 本次设备序列号 / MAC | `1C:29:04:D0:4A:B0` / `1c:29:04:d0:4a:b0` |
| 芯片 | `ESP32-S31`，镜像 chip id 为 `32` |
| 主应用 | `ThingsOS_native`，版本描述符与目标版本一致 |
| 救援 | `ThingsOS_rescue`，保留已有可用镜像 |
| 主应用输入文件 | 主应用 `.bin`，如 `build/native-idf/ThingsOS_native.bin`；不是完整 USB 合并包 |
| 启动选择输入文件 | `build/native-idf/ota_data_usb.bin`，长度必须为 `0x2000` |

必须从当前分区表核对以下布局；布局不一致时停止，不能盲用地址：

| 区域 | 起始偏移 | 分区大小 / 范围 |
| --- | --- | --- |
| S31 bootloader | `0x2000` | 按实际镜像长度；保护 `0..0x8000` |
| 分区表 | `0x8000` | 保护 `0x8000..0x9000` |
| NVS / 用户设置 | `0x9000` | `0x6000` |
| otadata | `0xf000` | `0x2000` |
| PHY | `0x11000` | `0x1000` |
| rescue / factory | `0x20000` | `0x200000` |
| ota_0 / 主应用 | `0x220000` | `0xae0000`，结束于 `0xd00000` |
| system_font | `0xd00000` | `0x300000` |

主应用文件必须装得进 `ota_0`。主应用 `.bin` 不能按完整 USB 包写到地址 `0`。
此流程保留设置，不执行全片擦除，不写引导、分区、PHY、救援或字体分区。
不改变 VCOM、TPS65185 EEPROM、充电策略及面板时序。

## 3. 连接前的检查

1. 保存候选文件的完整 SHA-256、大小、项目名、chip id、版本及 ELF 标识。
2. 保留可恢复的正式版主应用文件和对应哈希。
3. 关闭官网烧录页的设备连接及其他串口监视器；同一时间只允许一个程序访问设备。
4. 使用本次验证过的工具环境。不要在恢复过程中临时升级串口工具或换用未知 stub。
5. 每次使用独立的 `build/` 输出目录，避免覆盖旧备份、回读文件和故障日志。

本次一次性脚本把候选哈希、正式版基线、设备身份和输出路径都写成了断言。
换候选或重复安装前，必须检查并更新这些断言，不能简单删除检查或反复执行旧文件。

## 4. 本次成功的 Windows 串口处理

成功组合是：**保持同一串口会话、ROM 提示后立即同步、处理 S31 易失下载看门狗，
再加载官方 stub**。Windows 超时参数更新仅调用 `SetCommTimeouts`，避免重复应用
DTR/RTS 状态。该组合已成功完成两次安装；没有独立 A/B 分离每一项的因果贡献，
不能断言此前所有故障都只由看门狗、Windows 驱动或 USB 线中的某一项引起。

### 4.1 串口类与打开方式

以下为已使用的 Windows 专用辅助代码。`keep_line_state=True` 后只允许调整超时；
若需要改变波特率、数据位或流控，应先重新评估此封装，不能把它当作通用串口类。

```python
import ctypes
import serial
from serial import win32

class StableUSBSerial(serial.Serial):
    keep_line_state = False

    def _reconfigure_port(self):
        if not self.keep_line_state:
            return super()._reconfigure_port()
        t = win32.COMMTIMEOUTS()
        if self._timeout == 0:
            t.ReadIntervalTimeout = win32.MAXDWORD
        elif self._timeout is not None:
            t.ReadTotalTimeoutConstant = max(int(self._timeout * 1000), 1)
        if self._timeout != 0 and self._inter_byte_timeout is not None:
            t.ReadIntervalTimeout = max(int(self._inter_byte_timeout * 1000), 1)
        if self._write_timeout == 0:
            t.WriteTotalTimeoutConstant = win32.MAXDWORD
        elif self._write_timeout is not None:
            t.WriteTotalTimeoutConstant = max(int(self._write_timeout * 1000), 1)
        if not win32.SetCommTimeouts(self._port_handle, ctypes.byref(t)):
            raise serial.SerialException('SetCommTimeouts failed')

# device_port 必须由当前枚举结果及目标设备序列号确定。
s = StableUSBSerial(port=None, baudrate=115200, timeout=.05, write_timeout=1)
s.dtr = False
s.rts = False
s.port = device_port
s.open()
```

先设置 DTR/RTS，再打开端口；之后尽量沿用同一 `s`。本次通信参数为 115200，
没有靠提高波特率恢复连接。

### 4.2 快速 ROM 连接与 S31 看门狗

以下片段只连接下载器并调整芯片**易失**下载状态，不擦写 Flash。它应放在完整的
异常处理和 `finally: s.close()` 中，并在已核对的设备身份及分区流程内使用。

```python
import time
from esptool.reset import USBJTAGSerialReset
from esptool.targets.esp32s31 import ESP32S31ROM
from esptool.cmds import run_stub, attach_flash

rom = ESP32S31ROM(port=s)
s.keep_line_state = True
s.write_timeout = 1
USBJTAGSerialReset(s)()

boot = bytearray()
deadline = time.monotonic() + .7
while time.monotonic() < deadline:
    boot.extend(s.read(4096))
    if b'waiting for download' in boot:
        break

# 不在 ROM 提示后额外空等数秒。
rom.sync()

# S31 LP WDT / SWD 寄存器；取自本机 esptool 5.5.0 的 S31 映射。
rom.write_reg(0x20801018, 0x50d83aa1)
rom.write_reg(0x20801000, 0)
rom.write_reg(0x20801018, 0)
rom.write_reg(0x20801020, 0x50d83aa1)
rom.write_reg(0x2080101c, rom.read_reg(0x2080101c) | 0x40000)
rom.write_reg(0x20801020, 0)

assert ':'.join('%02x' % v for v in rom.read_mac()) == expected_mac
assert rom.get_chip_id() == 32
s.write_timeout = 10
esp = run_stub(rom)
attach_flash(esp)
```

后续 `read_flash`、`write_flash`、`erase_region` 使用这个已连接的 `esp`，
不要为每个操作另开一个串口进程。若已经启动 RAM 下载程序，应按明确的状态机
重新进入 ROM 或复用已知 stub，不能混用未知的下载器状态。

## 5. 正常主应用更新的完整顺序

### A. 确认基线并备份

若当前主应用正常，先捕获一次启动日志，确认主应用偏移、版本、ELF 和健康状态。
本次键盘安装还要求：备份中的原主应用字节与正式版镜像完全相同。

使用 `esptool.cmds.read_flash` 及上述 `esp` 读取：

```python
read_flash(esp, 0, 0x1000000, output=str(backup_path),
           flash_size='16MB', no_progress=True)
```

备份必须恰好为 **16,777,216 字节**，保存 SHA-256；另外保存原始
`0xf000..0x11000` 启动选择数据。备份未完成或身份/布局不符时不进入写入步骤。

### B. 先实际启动救援

备份后，在 ROM/stub 中仅清除 `otadata` 的 `0xf000 / 0x2000`，使现有 factory
救援被选择。此动作必须已有备份且确认救援镜像存在、项目名正确。

复位并捕获 **45 秒**日志，必须看到：

```text
Loaded app from partition at offset 0x20000
Recovery HTTP ready
```

还要确认没有 `SD install verified; starting sole main application`，避免救援
因为遗留安装任务自动离开。静置时间同时给救援的空闲启动保护清零逻辑留足时间。
只有确认救援实际运行后，才允许更新主应用分区；不能只清启动数据便假定已经切换。

再次按第 4 节连接 ROM/stub，读取并保存救援启动后的 NVS（`0x9000 / 0x6000`）。
分别记录它与原始备份是否一致，以及随后主应用写入是否改变它；不要把启动导致的
差异与 Flash 写入差异混为一谈。本次两者均未改变。

### C. 写主应用并校验，最后选择启动

1. 用 `write_flash(esp, [(0x220000, image_bytes)], ...)` 写主应用。
   本次参数：`flash_mode='dio'`、`flash_freq='80m'`、`flash_size='16MB'`。
   必须等写入及工具自身哈希校验返回成功。
2. 完整回读 16 MiB 至另一文件。目标主应用区域必须与候选逐字节相同。
3. 引导、分区表、PHY、救援和系统字体必须与备份一致；NVS 必须与救援启动后
   快照一致，并单独报告是否也与原始备份一致。
4. 此时启动选择区仍应为 `0xff`；任何校验失败都不能选择主应用启动。
5. 只有全部通过，才把正确的 `ota_data_usb.bin` 写到 `0xf000`。
6. 回读这 8192 字节，与选择器文件逐字节核对，随后才复位。

本布局的已验证选择器是 sequence=1、state=0、CRC=`0x4743989a`，完整文件
SHA-256 为 `7c0e7928b9f8d90fd5b0d4dcf7bf325674bf4367576345e4789c032e5475741f`。
此值用于核对此布局，不是任意板型/分区方案的通用启动数据。

### D. 启动验收与释放串口

使用 `HardReset(s, uses_usb=True).reset()` 请求正常启动，捕获 **45 秒**日志。
USB 复位可能重新枚举；遇到串口读错误时按同一 USB 序列号重新查找并打开端口，
继续捕获，**不要把重连变成再次复位**。

必须确认：

```text
Loaded app from partition at offset 0x220000
App version:      <目标版本>
ELF file SHA256:  <候选 ELF 前缀>...
UI ready: library
Startup health confirmed: display, font, touch
```

完整镜像回读验证哈希；启动 ELF 用于确认实际启动了哪份应用。不能仅以界面版本号
区分同为 0.1.0 的正式版和键盘候选。

检查 `Guru Meditation`、`assert failed`、`invalid segment`、`Panel draw error`、
`underrun occurred`；若出现，保留完整日志，按具体失败阶段处理。健康检查通过前
不要连续复位。最终在 `finally` 中关闭串口，交回用户测试。

## 6. 已遇到的失败与边界

| 现象 | 本次经验 / 处理原则 |
| --- | --- |
| `Write timeout`、`Serial data stream stopped` | 都是症状，不能直接断言 USB 线损坏。先确认唯一连接者、设备身份、ROM/stub 状态和完整日志，再采用第 4 节已成功路径 |
| 网页显示 `Guru Meditation` | 不能由网页这句话判断是下载 stub 还是主应用崩溃；保留设备端完整输出，不反复盲写 |
| 大块 `--no-stub` 备份中止 | 本次 ROM 无 stub 大块读取曾在约 26 KiB 处停止；成功的大块读取使用 Espressif stub |
| `native/flash-metalio.ps1` | 当前旧脚本仍把 `--no-stub` 用于完整备份，且顺序不等同于本文；本次没有修该脚本，不要直接视为已验证入口 |
| `Three unconfirmed starts` | 连续未确认启动可触发 boot guard 进入救援。正常启动/救援后留足 45 秒，不连续快速复位 |
| `invalid segment length 0xffffffff` | 本次确认主应用不完整。不要只改启动选择强行退出救援；先恢复并校验完整主应用 |
| 救援 HTTP OTA 断开 | 可能已经擦除/部分写入主应用。未收到成功响应时不能承诺“原固件保留”；需核对实际镜像/启动状态 |

`read-flash` CLI 参数次序是 `ADDRESS SIZE OUTPUT`，`--flash-size` 等操作参数应放
在对应子命令后。本文成功安装采用保持连接的 Python API，不依赖分进程 CLI 连接。

下载模式没有运行应用的常规电源管理，不要无目的长时间停留。发生阻塞时停止写入，
在已知安全的状态恢复救援或正常应用；不要为解决连接问题改 PMU 或面板电压。

## 7. 本次成功证据与耗时

| 安装 | 应用 SHA-256 | 结果 |
| --- | --- | --- |
| 恢复正式版 0.1.0 | `7202b7d8211ef08291daff60ee918dcd1616c6342c6e14340b1af52268f9406a` | 完整回读、保护区及启动健康通过 |
| 正式版上安装键盘候选 0.1.0 | `0408b86dd95e286cb07124e2786a04595b602c7a9f5be9b90f658bc1e5c1f1a5` | 42/42 主机测试；USB 回读与启动健康通过；键盘效果待用户测试 |

成功脚本和日志均在项目 `build/keyboard-followup-20261031/` 下：

- 正式版恢复：`restore_formal_usb.py`；证据目录 `formal-usb-recovery-2/`。
- 键盘候选安装：`install_keyboard_usb.py`；证据目录 `keyboard-usb-test/`。
- 上述脚本目前通过 `install_authorized.py` 引入 `StableUSBSerial` 与设备查找函数。
  **被引入文件的 `main()` 不是成功入口，不要直接执行它。**
- 每个成功目录包含 `result.json`、`before.bin`、`after.bin`、写入/回读日志和
  主应用启动日志。`build/` 可能被清理，后续使用前先核对文件仍存在。

键盘候选本次实测：16 MiB 备份约 **104.9 秒**，主应用写入约 **39.7 秒**，
16 MiB 回读约 **104.9 秒**；另有正常/救援/新应用各 45 秒的启动观察，整个流程
大约数分钟。读取期间没有逐行进度输出不等于失败，应以完成记录或异常为准。

键盘候选 ELF 为 `98408fb28526cf10991df84ec66e33ecd88bb6bba7217b2285b700ab6398b1d2`，
启动健康在 34918 ms 确认。用户设置、引导、分区、PHY、救援、字体均与备份一致。
成功安装与启动健康不能替代触摸准确性、显示残影、音频/网络或长期稳定性验收。
