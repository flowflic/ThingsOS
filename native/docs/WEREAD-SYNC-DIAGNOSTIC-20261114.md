# 微读同步页错误诊断

同步页在书架按钮下方显示两行：中文失败原因，以及可以截图反馈的诊断码。不再把一般请求失败一律解释为 WiFi 问题。

请求未启动的横幅分别提示：SD 卡不可用、WiFi 未连接、时间未校准、其他任务占用，以及未能创建任务（不凭此推断内存不足）。

已经启动的拉取/进度/计时会话任务保存独立诊断快照，包含最终业务错误及最后一次 HTTP 请求阶段。成功清除该同步任务的错误；无关的下载、封面、划线任务不覆盖快照。只在业务结果是失败时显示，不把取消标成网络故障。

诊断码：

- A：任务枚举（PULL=7，UPLOAD=8，ENTER=13，REPORT=14）。
- 接口类别：READER 阅读会话，CONFIG 计时配置，PULL 云端进度，POST 上传，AUTH 续期，OTHER 其他，PRE 未进行 HTTP 请求。
- P：最后阶段。0 请求前，1 域名预检，2 创建连接/连接握手，3 已连接，4 请求头已发出（请求体可能仍在发送），5 收到响应头，6 收到响应体。阶段反映最后观察到的事件，不冒充服务端已接受上传的证明。
- E：`thingsos_weidu_error_t` 业务结果；H：HTTP 状态，0 表示未取得；X：SDK 请求返回值（十六进制）；T/F：TLS 错误及校验标志；N：socket errno。

例如 `A8/POST P4 E1 H0 Xffffffff T0 F0 N104` 表示进度上传在请求头发送后未取得 HTTP 状态，SDK 失败，socket 错误 104。不能仅凭 H0 判定未向服务器发送任何内容，尤其不能据此自动重报时长。

中文提示区分域名失败、安全连接/证书失败、登录失效、时钟、内存、存储、容量上限、回执未确认、429 限流、5xx 服务异常、协议不兼容等。有状态码时按已取得的状态解释；无状态码时结合已观察阶段提示。诊断不含账号、书名、URL、Cookie、Token 或响应正文。

验证日志：`build/sync-diagnostic-host-build.log`、`build/sync-diagnostic-host-tests.log`、`build/sync-diagnostic-firmware-build.log`。控制器新增 HTTP/DNS/TLS 失败画面与诊断内容检查，成功后隐藏检查；渲染预览 `build/sync-diagnostic-preview.png`。测试与构建不代替现场复现。版本保持 0.1.7，未烧录、发布。

相关 CTest 3/3 通过（42.07 秒）；最终提示文字调整后的 UI 控制器再次通过（`build/sync-diagnostic-ui-final.log`）。主固件及 JPEG 应用符号检查通过，产物 `build/native-idf/ThingsOS_native.bin`。
