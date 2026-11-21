# ima 配对二维码与首页黑到黑校正（2026-10-30）

版本保持 `0.0.1-v1-beta`；未发布、未提交。本轮范围是三项用户明确提出的
问题：手机配对页看不到局域网地址与二维码、首页仍旧淡墨不均、首页切换需要与阅读页
同一套黑到黑校正。候选已按用户 2026-10-30 的授权安装到设备，证据见第 5 节。

## 1. 连接 ima 直接显示局域网地址二维码

此前「连接 ima」页只把 `https://<ip>:8443/` 打印成一行文字，用户必须手动抄到
手机浏览器；传输页早有 `qr_address_box()`。现在配对页直接显示扫码目标。

- 新增 `ima_pair_link()`：用 `thingsos_network_address(false, …)` 取 STA 地址，
  替换协议后再拼 `:8443/`。地址未就绪时返回空，不生成指向
  “等待网络地址” 的无效二维码。
- 二维码用既有 `qr_addressgen` 编码（`qrcodegen_Ecc_MEDIUM`，版本上限 10），
  300 px 见方，坐标经 `qr_address_box(link,240,300)` 居中在内容列内。
- 页面顺序调整为：提示 → 二维码 → 地址文字 → 配对码与有效期 → 证书指纹 →
  密钥说明 → 关闭按钮（热区随之下移到 `y=984..1052`）。
- 未联网时同一位置显示「连接网络后显示配对二维码」，不画空框。
- 配对页本身仍是 HTTPS 自签证书，浏览器首次访问需要用户本人核对屏幕指纹；
  二维码只携带地址，不含配对码、Client ID 或 API Key。

主机验证：`device_ui_test` 在 `view==2` 固定 8 位配对码与 32 字节指纹，
按像素检查二维码区域（`y=246..330`）有墨、配对码行有墨，并在
`host_network_connected=false` 时断言该区域完全无墨。快照
`build/native-fixtures/ui-ima-phone-pairing.pgm` 另用 OpenCV `QRCodeDetector`
解码为 `https://192.168.1.42:8443/`，说明版式与纠错级别可被通用扫码器识别。
该解码来自主机渲染的位图，不等于手机实拍；实拍仍需设备验收。

## 2. 首页淡墨不均的根因

阅读页的淡墨不均此前只修了普通翻页与水波纹（见
[黑到黑校正](READER-BLACK-CORRECTION.md) 与
[水波纹校正](READER-RIPPLE-BLACK-CORRECTION-20261029.md)）。首页走的是另一条分支：

- 非阅读、非键盘、非状态栏的 UI 帧进入 `ui_low_flash`，均衡模式用的是
  `text_waveform`，也就是 `thingsos_refresh_text_wave()` 生成的**未校正**低闪视图。
- 该视图把所有 `from==to` 动作清成保持，只保留一次白向维护。图标的黑色描边、
  标题与标签的笔画在前一帧已经是黑色，于是整帧保持——与阅读页修复前同一类缺陷：
  同一字形/图标里被重画的边缘与保持的部分深浅不一。

## 3. 首页与首页切换补齐黑到黑校正

- 板层把 `text_waveform` 与 `reader_text_waveform` 两张表合并为一张
  `corrected_waveform`（36 相，2304 字节 LUT，启动时生成）。
- 普通阅读翻页与均衡模式 UI 低闪帧都改用这张表；`thingsos_panel_ui_text_wave()`
  作为独立入口存在，主机测试断言它与 `thingsos_panel_reader_text_wave()`
  逐字节相同，避免两条路径漂移。
- 选择条件抽到 `thingsos_refresh_ui_corrected(mode,ready)`：只有均衡模式
  （`mode==0`）且校正表可用时才用校正表；快速模式的 DU20 视图、
  显式/周期全刷、清晰模式、休眠壁纸、键盘与错误恢复路径不变。
- 波形相位、扫描时钟、相位时间、供电流程、VCOM 与字体绘制均未改动，普通 UI
  帧的扫描次数不变。

日志：启动新增
`Black-to-black correction ready=1 phases=36 (reader turns and ordinary UI frames)`，
`UI low-flash GC16` 行新增 `black_corrected=`，`Refresh` 行新增
`ui_black_corrected=`。设备上应看到首页各 Tab 与下级页面切换为
`black_corrected=1`，全刷、键盘与水波纹仍为 0。

## 4. 主机验证

`native/tests`（`build/host-test-incremental.cmd`）40/40 通过：

- `waveform_actions`/`stagger_test`：36 相校正表仍为
  12 压黑 / 11 擦白 / 13 保持，只有 23 个黑到黑动作与低闪基线不同；
  新增断言 UI 入口与阅读入口逐字节一致，且错误相数、空指针被拒绝。
- `display_cache_test`：`thingsos_refresh_ui_corrected` 只对均衡模式且表可用
  返回真；UI 低闪与周期清理的既有预算行为不变。
- `device_ui_controller`：配对页二维码/地址/指纹版式与断网回退。

主应用与救援固件均编译通过（ESP32-S31，ESP-IDF v6.1）：

| 镜像 | 大小 | SHA-256 |
| --- | ---: | --- |
| `build/native-idf/ThingsOS_native.bin`（`0.0.1` 开发镜像） | 6,497,216 B | `7defce7a6b644dabec0b33c5145bb5c0e41b9e77807882ee2a42a4edd7852aa6` |
| `build/native-idf-0.0.1-v1-beta/ThingsOS_native.bin`（已安装） | 6,497,216 B | `8ff0310d81d6e30d7c33a7b0924aa86adf7f72fbdb95b1d3affb036b51bbd8c4` |
| `build/native-rescue/ThingsOS_rescue.bin` | 1,350,208 B | `8bc427d02832353ccba38d2bb8e3beb40d9b0ce2b88307a1cc15a3253d1b83e7` |

分区检查仍报告 `rescue` 分区容量小于主应用镜像——这是既有布局的事实，
主应用写入 `ota_0`（0xae0000）不受影响。

## 5. 已授权安装的设备证据

用户于 2026-10-30 明确要求“烧录到设备”。目标为 Metalio COM5，USB 序列号
`1C:29:04:D0:4A:B0`。脚本 `build/flash-ima-qr-home-black-20261030/install.py`
（失败后由 `resume.py` 接续）按以下顺序执行，任一校验不符即停止：

1. 完整读取 16 MiB 备份，SHA-256
   `cee3a429cee160ce9278a24b2aa521bbd53f43236b370e7c0dae439093e5ac5c`
   （`before.bin`）。安装前主应用为上一轮 `0.0.1-v1-beta` 镜像，启动选择
   `seq=1`、`state=0xffffffff`（valid）。
2. 仅写主应用 `0x220000`，随后写 OTA 选择区 `0xf000`（`seq=1`、`state=0`）。
3. 回读主应用与选择区，逐字节匹配；回读 `0x2000..0xf000`（bootloader、
   分区表、PHY、NVS），除 `0xf000` 选择区外全部与备份一致；救援分区
   `0x20000..0x220000` 与系统字体 `0xd00000..0x1000000` 也回读一致。
4. 硬复位启动，启动日志确认：

```
boot: Loaded app from partition at offset 0x220000
app_init: App version: 0.0.1-v1-beta
app_init: ELF file SHA256: eab7deab0...
ThingsOS_board: Black-to-black correction ready=1 phases=36 (reader turns and ordinary UI frames)
ThingsOS: UI ready: library; initial screen update succeeded
ThingsOS: Startup health confirmed: display, font, touch; single app, no version rollback
```

启动窗口无 `underrun occurred`、无 `E (` 错误、无安全时钟降级。首帧全清
GC16 693 ms；首次普通 UI 低闪帧

```
UI low-flash GC16: full-pixel white maintenance tab_cleanup=0 black_corrected=1 result=0x0
Refresh GC16: 529 ms, clean=0, soft=1/15, ... pclk=20 MHz black_corrected=0 ui_black_corrected=1
```

说明首页/下级页面已走校正表，耗时与上一轮普通翻页（约 529 ms）一致；
`black_corrected=0` 与 `ui_black_corrected=1` 同时出现，是两条路径共用
一张表但分别标记的结果。设备启动后获得 STA 地址 `192.168.31.16`，
与配对页应显示的 `https://192.168.31.16:8443/` 对应。

安装结果保存在 `build/flash-ima-qr-home-black-20261030/flash-result.json`，
其中 `physical_uniformity_verified`、`physical_qr_scan_verified` 均为
false——串口证据只证明执行路径，不证明光学均匀度或手机能否扫码。

## 6. 设备验收边界

主机测试、日志与安装核对只证明版式与波形构造，不证明屏幕观感。请在设备上确认：

1. 录音应用右上「ima 设置」→「手机配置」，用手机扫描页面上方的二维码，
   能否直接打开 `https://192.168.31.16:8443/`；浏览器证书提示中的 SHA-256
   是否与屏幕四行指纹逐位一致（不一致请勿输入凭证）。
2. 首页四个 Tab 往返、进入/退出下级菜单，观察图标描边与标题笔画是否与阅读页
   同等均匀；确认此前首页的淡墨不均是否消失。
3. 快速模式首页、显式全刷、休眠唤醒与阅读水波纹的观感不应回退。
4. 如再抓串口，普通 UI 帧应持续为 `ui_black_corrected=1` 且无新增欠载。
