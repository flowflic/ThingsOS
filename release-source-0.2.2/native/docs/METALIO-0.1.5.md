# ThingsOS Metalio 0.1.5（2026-11-03）

把项目及默认主/救援固件版本统一升级到 **0.1.5**，纳入 0.1.4 发布之后完成的修正。

## 本版改动

- **版本**：项目及默认主/救援固件版本统一为 0.1.5。
- **充电**：闪电图标移入电池框内（14×17），随电量填充反色；修复电源管理芯片 DPDM 关闭时把充电误判为未充电，以及开机第一分钟内充电状态变化不触发刷新的问题。`charge_status_decode` 按 `vbus == 7 → NONE`、`stat != 0 → CHARGING`、`vbus == 0 → NONE`、否则 `percent >= 99 → FULL` 判定，电源管理驱动只在状态变化时打印原始 `REG_STATUS1` 字节。
- **Wi-Fi**：射频启动后请求 12 dBm 发送功率上限（`WIFI_TX_POWER_LIMIT_QDBM` 48）并回读确认，STA/AP 启动事件与热点切换后重新应用，确认失败即停止该次射频启动；STA 协议由 b/g 扩展为 b/g/n。
- **微读**：`radio_needs_awake` 只在实际联网交换时唤醒射频，其余时间恢复省电；worker 记录动作、网络状态、错误与实际耗时，用于区分省电策略问题与网络问题。
- **EPUB 内嵌图片**：图片按原图比例排版、按正文宽度适配；长图给出提示，点开后可在查看页上下拖动，查看页按钮由"适应屏幕"改为"适应宽度"。新增有界内存的 PNG 流式解码（`reader_png.inc`）、JPEG 缩放解码与两项、上限 2 MiB 的解码缓存（关书清空）；修复快速 IDCT 有符号值未饱和导致灰度图片白底变黑。
- **阅读帧电源**：阅读、休眠与出错帧绘制完成后立即关闭显示屏电源轨并校验空闲，不再复用 3 秒窗口，减少静置耗电。
- **微读元数据**：章节元数据改用以截断长文本方式扫描的 JSON 结果，避免整章正文进入内存。

## 构建与校验

源码为提交 `db9c8379feea0d28a5e644ceaf2c4b62bd9ffe23`（2026-11-02 23:24:52 +0800）的分离工作树，仅把 `native/VERSION` 改为 0.1.5 后构建。

```powershell
Remove-Item Env:MSYSTEM   # MSYS 环境下 export.ps1 会拒绝运行
./native/build.ps1 -Rescue
./native/build.ps1
python ./native/package_metalio.py
```

- `package_metalio.py` 校验通过：分区表、chip id、工程名、主/救援版本一致、字体魔数 `THFONT01`。
- USB 合并包 16,610,104 字节，SHA-256 `2c5c5067a51ee142fe7598fbc90a3bcd255f7f58879299ec0039f68e1a7927de`。
- OTA 应用镜像 6,523,072 字节，SHA-256 `701a918be0fce27b6bacd11721672beb344d48eca6f0236f15a0ee351ec8ecc1`；救援镜像 1,360,800 字节，字体 2,978,616 字节。
- 本次发布构建未运行 `verify_build.py` 与主机回归：`build/native-host-cmake` 的缓存中 `CMAKE_C_COMPILER` 为 `CMAKE_C_COMPILER-NOTFOUND`，本机没有可用的主机 C 编译器。0.1.4 的 45/45 主机回归结果属于上一版源码，不能当作本版证据。

## 设备验收

0.1.5 与已安装镜像源代码相同，该镜像以 0.1.4 编号安装，并通过完整回读、保护区与 NVS 核对、启动健康检查；电池框内闪电、插拔判定、EPUB 内嵌图片与长图拖动、阅读静置耗电已在该机确认。0.1.5 编号镜像本身只完成打包与发布，尚未安装。

## 官网发布（2026-11-03）

- 站点 https://thingsos.scholarcn.com 由 Vercel 项目 `thingsos` 发布，部署 `dpl_GyinfPryZyHr1nZBhayE9XK4DEUV`。
- 生产读回校验：`metalio/release.json` 版本 0.1.5、`previous_version` 0.1.4、7 条更新；USB/OTA 下载字节数与 SHA-256 与清单一致；`metalio/manifest.json` 指向 0.1.5 USB 包；页面版本标记为 0.1.5。
- 历史更新记录只保留过去版本：列表从 `v0.1.4` 开始，未加入 `v0.1.5` 条目。
- 旧 0.1.4 固件地址返回 404。
- 更新说明已由用户确认后发布。
