# 在线微读进度与时长同步失败修复

## 证据与边界

用户照片显示 WiFi 图标、进度上传“网络请求失败”、阅读时长“暂无上传记录”。没有该用户的串口日志、HTTP 状态或确切固件指纹，不能把照片中的这一例直接归因为某个网络阶段。

代码检查及故障注入确认两个可导致同类现象的问题：

1. `reader_transport_write()` 未为正数短写设置 `errno=EAGAIN`，也没有将 TLS `WANT_READ/WANT_WRITE` 转成异步续写。当前本机 ESP-IDF 6.1 的 `esp_http_client_send_post_data()` 遇到正数短写返回 WRITE_DATA；`esp_http_client_perform()` 仅在 errno 为 EAGAIN 时保留状态供下一轮继续。原适配器因此会提前结束 POST，而正文 GET 不经过请求体发送阶段。新增短写回归在旧适配器稳定失败（`transport line 69`），修复后通过。
2. UI 在首次 ENTER 提交前就清除 `wd_enter_pending`，失败后没有恢复路径。在线 Web 时长要求先建立会话；首次临时失败会令本次打开一直缺少 `session_ready`，后续 30 秒上报不启动。

## 修改

- 保留短写的已发送字节数，用 EAGAIN 让 SDK 在同一请求的原游标继续；TLS WANT_READ/WANT_WRITE 同样续写。每次底层写前清除旧 errno，避免上轮 EAGAIN 掩盖真实终止错误。
- 首次会话建立遇到 NETWORK/DNS，5 秒、15 秒后各重试一次，总共最多三次。只有成功提交任务才消耗尝试次数；退出/换书不会继续本书的重试。
- ENTER 返回的错误写入进度状态。成功后从成功建立时开始计时，不把建立会话前的时间追补成在线时长。
- 不重发已经提交的时长 POST；保留原时长队列、未知回执和计时归属边界。Native 离线时长通道不改变。
- 非敏感日志区分 read-config、read-upload、read-progress、renewal，仍不记录凭据、请求正文或完整 URL。

## 验证

传输测试覆盖短写、TLS WANT_READ/WANT_WRITE、终止错误和旧 errno；UI 控制器覆盖首次失败、5/15 秒退避、最多三次和请求不带时长。证据日志位于 `build/sync-host-build.log`、`build/sync-host-tests.log`、`build/sync-fix-firmware-build.log`。

相关回归 5/5 通过（46.68 秒）：weidu_workflow、device_ui_controller、weidu_notes_progress_time、reader_http_scan_serialization、reader_network_bounded_waits。

主固件构建及 JPEG 应用符号检查通过；候选 `build/native-idf/ThingsOS_native.bin` 同时包含此前加载条纹的错误恢复修改。

保持 0.1.7，不提交、不发布、不烧录。主机故障注入和构建不替代用户现场复测或真实账号云端入账确认。
