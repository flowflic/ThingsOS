# ThingsOS Metalio 0.1.0 源码/构建快照（2026-10-31）

2026-10-31，按用户要求将**项目版本**与**默认主/救援固件版本**统一更新为
`0.1.0`，并提交当前源码快照。本文只记录版本口径与验证边界；功能实现与
设备验收记录仍以各功能文档为准。

## 版本来源与生效范围

`native/VERSION`（内容 `0.1.0`）是唯一版本来源，以下位置都从它读取，
不再各自硬编码：

| 文件 | 用途 |
| --- | --- |
| `native/CMakeLists.txt` | 主机核心库 `ThingsOS_native_core` 的项目版本 |
| `native/firmware/CMakeLists.txt` | 主应用 `PROJECT_VER` 与项目版本 |
| `native/rescue/CMakeLists.txt` | 救援固件 `PROJECT_VER` 与项目版本 |
| `native/build.ps1` | 默认构建版本；`-Rescue` 只允许与项目版本一致 |

`native/build.ps1 -Version <ver>` 仍可显式覆盖主应用版本用于候选构建，
并输出到 `build/native-idf-<ver>/`；救援固件版本固定取 `native/VERSION`，
因为版本回滚已禁用、救援分区只有一个。

设备端显示同一来源：设置页「固件升级」读取
`esp_app_get_description()->version`（`native/firmware/main/main.c`），
启动日志打印 `App version:`。

## 本次快照包含的改动

提交内容为此前各轮已记录、但尚未提交的工作区改动，加上本轮版本化：

- 阅读、刷新与显示：黑到黑校正表扩展到家目录与下级菜单、水波纹校正、
  欠载自动恢复 20 MHz。
- 传输与存储：手机上传工作区优先使用 PSRAM、SD 卡 DMA 预留、
  文件管理音乐删除、传输进度与锁屏波纹。
- 录音与 ima：ima 配对页排版与扫码引导、HTTP 直接配置与持久连接、
  临时凭证解析、JPEG 解码器独立符号与链接检查、录音上传。
- 应用：闹钟（日历排程、重复规则、NVS 持久化）、应用控制页、
  音量浮层连续调节计时。
- 输入与网络：Metalio 晃动安装轴 `(Y,-X,Z)`、播放音乐时 WiFi 重连判定。

各轮的主机测试数量、构建结论和**设备验收边界**逐项写在对应文档中，
本文件不复述、也不升级这些结论。

## 0.1.0 镜像

2026-10-31 按 `native/VERSION` 构建，未写入设备：

| 镜像 | 大小 | SHA-256 |
| --- | --- | --- |
| `build/native-usb/ThingsOS-metalio-0.1.0-usb.bin` | 16,610,104 B | `d885a75ea872c4ac4c8f97e92fc197a502963e51580d98da707da754cb4ac3c2` |
| `build/native-usb/ThingsOS-metalio-0.1.0-ota.bin` | 6,490,048 B | `7202b7d8211ef08291daff60ee918dcd1616c6342c6e14340b1af52268f9406a` |

主应用与救援固件描述符均为 `0.1.0`，项目名分别为 `ThingsOS_native`
与 `ThingsOS_rescue`；主应用位于 `ota_0`（`0xae0000`，占用 0x6307c0），
救援位于 `rescue`（`0x200000`，占用 0x1499d0，剩余 36%）。
分区表、`ota_0` 偏移、U 盘安装文件集合、`ota_data_usb` 选择器与系统字体
校验通过；NVS 在完整镜像中保持擦除；JPEG 解码器链接检查通过
（`thingsos_jpeg_prepare` / `thingsos_jpeg_decomp` 均落在应用内）。
清单见 `build/native-usb/metalio-manifest.json`。

`native/verify_build.py` 仍属 Read Pico / ESP32-S3 站点流水线，按
[METALIO-PORT.md](METALIO-PORT.md) 不适用于本目标：它断言 bootloader 在
`0x0`（S31 为 `0x2000`）并要求 `references/` 检出带 git 元数据。本轮用
等价的分区、描述符、字体与选择器检查替代，未使用该脚本的结论。

## 验证状态与边界

- 版本化改动本身只改变版本号与构建配置，未改波形、电压、充电策略或
  分区布局。
- 主应用与救援固件已用 0.1.0 重新构建，描述符核对通过（见上方
  「0.1.0 镜像」）。
- 历史上已安装到设备的镜像是 `0.0.1-v1-beta` 系列；0.1.0 镜像
  **尚未安装**。此前记录的设备验收、回读与启动健康结论属于那些
  beta 镜像，不能直接等同于 0.1.0。
- 本轮未发布网站、未改官网清单、未刷写设备。`native/package_site.py`
  与 `native/release-notes.json` 之外的发布资产保持原状；
  `package_site.py` 的版本断言仍属于 Read Pico / ESP32-S3 站点流水线，
  按 [METALIO-PORT.md](METALIO-PORT.md) 的说明，Metalio 打包使用
  `native/package_metalio.py`，暂不走该流水线。
- 光电、触控、音频、蓝牙与长稳验证需要实机；确认前不得标注为
  可发布或 flash-ready。

## 复现

```powershell
./native/build.ps1 -Rescue
./native/build.ps1
python ./native/package_metalio.py
```

以上命令只构建与打包，写入 `build/`，不连接设备、不烧录、不发布。
刷写设备需用户显式授权。
