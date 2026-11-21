# IMA 认证错误与微读封面崩溃（2026-10-30）

版本保持 `0.0.1-v1-beta`。修复版已按前续烧录授权继续安装，未发布或提交。

## 1. IMA 的 ESP_ERR_NOT_SUPPORTED

已安装代码调用 ESP-IDF v6.1 `esp_http_client_perform()`。SDK 在 HTTP 401 分支
调用 `esp_http_client_add_auth()`，试图识别 Basic/Digest；缺少认证挑战头或不支持
该类型时返回 `ESP_ERR_NOT_SUPPORTED`，早于完整响应体读取。

使用无效的诊断占位凭证（未读取/发送用户真实凭证）访问官方只读知识库列表接口，
得到 HTTP 401、无 `WWW-Authenticate`，响应为 code 200002、`skill auth failed`。
这与 SDK 的报错路径吻合；本轮未捕获用户那次请求的 HTTP 状态，因此不能据此
断言具体是哪一项真实凭证错误。

`ima_rpc.inc` 改用 open/write/fetch_headers/read 流式接口，不走 Basic/Digest
自动处理，保留 API Key 请求头、HTTPS 证书验证和禁止重定向。401/403/429
转换为明确的用户提示与非零 API 错误响应。5xx 仍返回传输不确定，防止添加请求
可能已提交时被错误标为可直接重试。响应上限仍为 64 KiB，短读/短写、取消、
截断和超限均检查，消息不输出凭证。

## 2. 微读崩溃的现场证据

设备最初已进入救援模式，串口无保留日志。保存已安装 ELF 后，一次受控重启
回到主应用且通过启动健康检查。开启被动日志记录后，用户再次点击微读，即重现：

- 从书库导航到微读，已加载 4 行书架；
- Core 0 `Load access fault`，RA `0x40067712`、MTVAL `0xcccce75d`；
- 旧 ELF 解码：`thumbnail_input` → `thingsos_decode_thumbnail` →
  `cover_from_mode` → `weidu_cover`；
- 随后重启及引导阶段异常，进入既有救援程序。

原始证据：`build/weread-login-crash-20261030/login-reproduction.log`，
旧 ELF：同目录 `ThingsOS-installed.elf`（已安装 ELF SHA-256 前缀 `9b766b062`）。
账号和设置没有清除，采集过程中没有刷写固件。

## 3. 根因与修复

S31 SDK `esp32s31.rom.ld` 无条件定义：

```text
jd_prepare = 0x2f800110;
jd_decomp  = 0x2f800114;
```

ThingsOS 本地 TJpgDec 使用同名公共函数，但具有自定义灰度/缩放配置及 `JDEC`
结构。原固件链接图最终将这两个符号放在 ROM 地址，本地实现被丢弃。
应用按本地结构调用 ROM 实现，读取错误的输入上下文而崩溃。主机没有 ROM
链接脚本，所以此前普通解码测试未暴露该问题。

修复使用独立的 `thingsos_jpeg_prepare` / `thingsos_jpeg_decomp` 符号。
`jpeg_decoder.c/.h` 包装并编译未改动的固定版本 TJpgDec；供应商文件、ChaN
版权许可与配置保留不变。缩略图调用显式使用独立符号，并补输入游标边界检查。
主构建增加 `verify_jpeg_link.py`，只有两个函数均位于应用代码区域才通过。

## 验证

主机 42/42 通过。内容测试提供模拟平台的同名 `jd_prepare/jd_decomp`，确认
本地解码不会调用它们，且实际 JPEG 缩放像素/尺寸仍符合预期。
新的 IMA 传输测试执行生产代码，覆盖认证头、短读写、401/403/429、重定向拒绝、
5xx 不确定结果、取消、截断和响应上限。

构建日志 `build/ima-auth-jpeg-firmware-build.log`，测试日志
`build/ima-auth-jpeg-host-tests.log`。真实微读封面、登录与 IMA 凭证验证仍需
修复候选安装后的设备验收，不把主机通过或启动健康检查当成现场功能验收。

## 安装与微读现场复测

修复版 6,488,912 字节，应用 SHA-256
`5ee175600a657ee540afa2bbaf2644fcc0cff94a323b316610e73a87a5ca9277`，
ELF `5fbd11d7d91b5f2441f8f01f01f3bccab9c5c3bf0161fa393deb8e71fe50b051`。
两个本地解码入口地址为 `0x400685e4`、`0x400689f4`，均在应用代码。

COM5 经完整备份、救援启动确认、写入、完整回读逐字节比对后选择主应用启动。
NVS 与初始备份一致，引导区、分区表、PHY、救援和字体均未改变。
本次启动 UART 捕获缺少早期 app_init 的版本/ELF 打印，最初严格日志检查因此失败；
未再复位打断用户，而以完整回读中的版本/ELF 描述符及已记录的 ota_0 加载位置
核对身份。日志仍明确包含 UI ready 与 Startup health confirmed，无 panic 或欠载。
此处不是把缺失日志误报为错误版本，也没有省略固件身份校验。

启动后用户点击微读，日志记录 4 张 JPEG 封面成功解码、微读页布局及后续刷新。
用户确认「书架正常显示，未再崩溃」。该项现场验收通过；不等同于微读全部功能或
长期稳定性验收。IMA 真实凭证验证仍待用户复测。
证据：`build/flash-ima-auth-jpeg-20261030/flash-result.json` 与 `beta-boot.log`。
