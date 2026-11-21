# ima 手机配对页打不开：根因与修复（2026-10-30）

版本保持 `0.0.1-v1-beta`。本轮修扫码打不开配对页，以及播放音乐时无法进入配对。

## 现场证据

用户照片显示配对页：

- 二维码已画出，下方地址 `https://192.168.31.16:8443/`；
- 「配对码：正在准备…」、有效期「0 秒内有效」；
- 屏幕底部提示「请先连接 WiFi，并结束当前任务」。

这四项合起来只有一种解释：`thingsos_ima_pair(true)` 返回了 false，
`thingsos_ima_pair_poll()` 从未启动 `httpd_ssl_start()`，8443 端口没有监听；
配对码与有效期都还是初始值。页面却按「地址非空」把二维码画了出来，
手机扫到的是一个没有任何服务的地址。

## 两个缺陷

### 1. 音频占用被当成传输，配对被拒绝

`thingsos_ima_pair()` 原判定：

```c
if(enable&&(!thingsos_network_connected()||thingsos_ima_active()||
            thingsos_audio_active()||thingsos_network_busy())){
    message("请先连接 WiFi，并结束当前任务");return false;}
```

`thingsos_network_busy()` 等于 `busy || external_busy`，而音频会话从开始播放
到 `finish()` 一直持有 `external_busy`。用户正播放音乐（或刚停止、会话未收尾），
于是配对被拒。配对页只从 flash 返回一个静态 HTML 文件，不写 SD、不占用传输通道，
与音频不冲突——与 WiFi 页那次误判同一类问题。

### 2. 服务未启动仍显示二维码

`view==2` 的绘制只看 `link[0]`（地址字符串非空），不看 `s->pairing`
（服务是否真的在监听）。所以「配对服务启动失败」与「正常配对」在屏幕上无法区分。

## 修改

- `native/include/thingsos/ima_gate.h`：把允许条件写成可测的纯函数
  `thingsos_ima_pair_allowed(connected,uploading,wifi_busy)`，
  即「已连接 ∧ 无上传 ∧ 无真实传输」，音频会话不再参与判定。
- `ima.c::thingsos_ima_pair()` 改用该判定；错误消息按原因区分
  （未联网 / 传输或上传进行中），不再统一说「结束当前任务」。
- `ima.c::thingsos_ima_pair_poll()` 的失败分支补日志：
  `httpd_ssl_start port 8443 failed: <err>`、证书生成失败、无连接；
  成功时打印 `Pairing HTTPS listening on port 8443`。
- `ima_ui.inc`：只有 `s->pairing && link[0]` 都成立才画二维码、配对码与指纹；
  否则显示「正在准备手机配对…」及原因，不再让用户扫一个空端口。
  另加一行说明：自签证书只能由系统浏览器确认，
  **微信内置浏览器无法绕过证书提示**，用微信扫码会表现为「打不开」。

## 验证

主机 `device_ui_test` 新增/扩展：

| 场景 | 期望 |
| --- | --- |
| `pairing=false`（服务未启动）但地址可用 | 二维码区域无墨（`qr==0`） |
| `pairing=true` | 二维码、配对码、指纹正常 |
| 未联网 | 无二维码 |

先临时把绘制条件改回只看 `link[0]`，该用例在
`device_ui_test.c` 的 `qr==0` 断言失败；恢复后 40/40 通过，说明测试确实覆盖缺陷。
`display_cache_test` 另覆盖 `thingsos_ima_pair_allowed` 四个分支
（已连接/未连接/上传中/真实传输）。

以上是主机与构建证据。**设备端 TLS 握手与手机浏览器行为未在本轮取得实测**：
安装后从主机向 `192.168.31.16:8443` 发起 TCP 连接超时，当时设备已进入休眠、
WiFi 已停——这本身不能区分「服务未监听」与「设备在睡」。因此
`flash-result.json` 里 `physical_pairing_verified` 记为 false。

## 设备复测步骤

1. 唤醒设备并保持亮屏（勿让它自动休眠），进入 录音 →「ima 设置」→
   「手机配置」。
2. 确认屏幕不再出现「正在准备…」与「0 秒」，配对码为 8 位数字、倒计时从
   300 秒开始；此时串口应出现 `Pairing HTTPS listening on port 8443`。
3. 用**系统相机或系统浏览器**扫码（不要用微信扫），
   应出现证书警告，核对屏幕上的 SHA-256 四行后继续。
4. 若仍打不开，请把串口日志中 `ThingsOS_ima` 相关行发我：
   `httpd_ssl_start port 8443 failed` 会直接给出失败原因。
5. 播放音乐时再进一次配对页，现在应当能正常打开（这是本轮第二个修复点）。
