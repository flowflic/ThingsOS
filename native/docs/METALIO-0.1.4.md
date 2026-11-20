# ThingsOS Metalio 0.1.4（2026-11-02）

把项目及默认主/救援固件版本统一升级到 **0.1.4**，纳入 0.1.3 发布之后完成的实机修正。

## 本版改动

- **检查新固件**：单次连接超时由 20 秒降到 5 秒，失败后最多重试 3 次、间隔 300 毫秒；原来失败后固定等待 10 秒再试一次。设备拿到 IP 后在后台预热到发布主机的连接（每 2 秒一次真实 TCP 连接，成功后停止），把开机后首次外网连接的等待提前。检查期间分别显示"正在连接网络…"与"正在获取更新信息…"。发布地址改为单一常量 `THINGSOS_RELEASE_HOST`，`THINGSOS_RELEASE_ORIGIN` 由它拼出。
- **充电图标**：状态栏电量左侧新增闪电图标（14×18，中心与电池框对齐），仅在电源管理芯片报告充电中时显示，每 5 秒检测一次状态变化；插电已充满时不显示。
- **EPUB**：清单与书脊上限由 1024 提高到 2048，可打开 1600 条目级的长篇全集（实测《诡秘之主（精校全本）》1609 个 item / 1486 章）；目录重排的临时数组由主任务栈移到 PSRAM，避免大书打开时栈溢出。
- **Wi-Fi 日志**：连接期间驱动每 22 毫秒输出一次的 `"Haven't to connect to a suitable AP now!"` 降到 ERROR 级，串口不再刷屏。
- **已知问题**：开机联网后存在一段 TCP 建连停滞，旧样本中首次观察到成功约在 DHCP 后 26–86 秒；部分同窗口 UDP/53 请求有正常回复。根因尚未定位；本版用预热与短超时重试缓解。NTP 走 UDP/123，其收包与重试延迟需独立判断；TCP 停滞会影响 HTTP Date 校时兜底。系统时间无效时左上角不显示日期时间，校时成功后的静止页面还可能等待下一次分钟刷新。见 [TCP 定位与官方/第三方源码对比](TCP-COLD-START-20261102.md)。

## 构建与校验

```powershell
Remove-Item Env:MSYSTEM   # MSYS 环境下 export.ps1 会拒绝运行
./native/build.ps1 -Rescue
./native/build.ps1
python ./native/verify_build.py
python ./native/package_metalio.py
```

- 主固件 `build/native-idf/ThingsOS_native.bin` 6,514,000 B，SHA-256 `14175f65eda3754520ab29c082adbd6bc24dea8c6918c60f2c8e3770439d4354`；救援固件 `build/native-rescue/ThingsOS_rescue.bin` 1,358,080 B。两者应用程序描述符均为 `0.1.4`。
- `native/verify_build.py` 写入 `build/native-idf/validation.json`：主机回归 45/45 通过，官方参考 checkout 仍是 `ed1e9331077451a0391769cd6c191568f7901807` 且未改动，编译来源边界通过，应用程序描述符与 `native/VERSION` 一致。
- 该脚本此前按 Read Pico（ESP32-S3）树编写，本树首次运行需移植：工具链路径取自 `THINGSOS_TOOLS`（默认 `D:\crossmux\.tools`），参考 checkout 与 git 溯源分离（本树 `references/read-pico-official` 无 `.git`，与固定提交逐字节比对），目标名改为 `esp32s31`，生成的 `project_elf_src_*.c`、`waveform-workspace/`、`reader-tls/` 与构建根目录的嵌入资源计为生成文件，bootloader 偏移改为 `0x2000`，刷新调度检查改为 Metalio 的 GC16 stagger 实现。
- USB 整包 `build/native-usb/ThingsOS-metalio-0.1.4-usb.bin` 16,610,104 B，SHA-256 `25aa453af11ef1a36649f61313170e77e23cdeef9f996b2d43f59afc535c557f`；OTA `build/native-usb/ThingsOS-metalio-0.1.4-ota.bin` 6,514,000 B，SHA-256 `14175f65eda3754520ab29c082adbd6bc24dea8c6918c60f2c8e3770439d4354`。整包 7 个分区（bootloader、partitions、nvs、otadata、rescue、app、font）逐段核对 SHA-256 与 MD5，nvs 段全为 `0xff`。

## 官网发布

- 发布到生产环境，部署 `dpl_5HFiXpCoWJtr1cnuFN1XUhKrRHxZ`（2026-11-02）。
- 线上校验（证据 `build/publish-metalio-0.1.4/online-verification.json`）：`metalio/release.json` 与 `metalio/manifest.json` 均为 0.1.4、上一版 0.1.3；USB 与 OTA 下载后逐字节哈希与发布记录一致；0.1.3 的两个固件文件已下线并返回 404；页面 8 处 0.1.4 标记、含 `v0.1.4` 历史条目；Read Pico 仍为 0.4.0，未改动任何 Read Pico 资源。

## 实机状态

- 与 0.1.4 同源的代码以 0.1.3 编号在 Metalio 实机安装，通过完整回读、保护区核对与启动健康检查；充电闪电、检查新固件与打开《诡秘之主（精校全本）》已在该机确认。
- 0.1.4 编号镜像已构建、打包并发布，尚未安装到设备。

## 后续充电状态修正（2026-11-02，本地候选）

用户复测反馈插充电头仍不显示，发现原驱动在 DPDM 关闭时错误使用 `VBUS_STAT=0` 否决真实充电，以及开机第一分钟的状态变化刷新条件错误。已修复，并按已确认原型把闪电移入电池框内。主机 48/48、主/救援固件构建通过；随后按用户授权安装此本地候选，完整回读和启动健康通过，实机捕获寄存器组合且用户确认闪电、插拔正常。版本仍为 0.1.4，官网发布包未替换。详见 [充电状态定位](CHARGING-STATUS-20261102.md)、[本次实机验证](CHARGING-WIFI-DEVICE-20261102.md)。

## 后续本地插图与静置修正（2026-11-02，版本不变）

《诡秘之主》RGBA PNG 连续内存不足、超大地图超限，改用有界行解码/块缩放；
修正快速灰度 IDCT 溢出造成的白底变黑，普通 JPEG 保留原路径并缓存解码结果。
阅读刷新恢复立即断轨；默认按正文宽度排图，长图支持点开后上下拖动。
50/50 回归、原书 114/114 图及主/救援构建通过，最终候选已按授权安装并完整回读、
保护区/NVS/启动健康通过；用户确认显示、速度、静置、默认宽度与拖动正常。
官网原 0.1.4 发布包未替换。详见 [定位、逐轮修正与实机验收](EPUB-INLINE-IMAGES-20261102.md)。
