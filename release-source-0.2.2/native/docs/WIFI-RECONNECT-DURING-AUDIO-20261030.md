# 播放音乐时 WiFi「再次连接」误报传输中（2026-10-30）

版本保持 `0.0.1-v1-beta`。本轮只修一处误判提示，不改波形、刷新策略、电源或音频时序。

## 现象

用户报告：播放音乐时，在 WiFi 页点击「再次连接」，页面下方提示
「正在传输，请完成后调整网络」；停止播放后再次点击仍然提示同一句话。

## 根因

有两个独立条件同时成立，缺一都不会复现：

1. **音频会话持有网络外部占用**。`thingsos_audio` 在 `start()` 里调用
   `thingsos_network_external_begin()`，并在 `finish()`（停止/出错/播完）才
   `thingsos_network_external_end()`。因此从开始播放到结束播放，`external_busy`
   一直为真。暂停（`CMD_PAUSE`/`CMD_SUSPEND`）不结束会话，占用仍在。
2. **WiFi 页用的是最宽的忙判定**。网络层内部给 WiFi 操作用的是窄判定
   `wifi_operation_busy()`，它把「音频自己持有占用」排除在外：

   ```c
   static bool wifi_operation_busy(void){
       return busy||(external_busy&&!(thingsos_audio_active&&thingsos_audio_active()));
   }
   ```

   但 `thingsos_network_mode()`、`thingsos_network_disconnect()`、
   `thingsos_network_reconnect()`、`thingsos_network_forget()` 调用前，
   UI 先判 `thingsos_network_busy() = busy || external_busy`，于是把音频占用
   当成了「传输中」，在真正调用网络层之前就 return 了。

「停止播放后仍然提示」对应的是停止到会话真正结束之间的窗口：`CMD_STOP` 只是
入队，工作线程要等当前帧写完、关闭文件、`finish()` 释放占用，在此之前
`thingsos_audio_active()` 与 `external_busy` 都还是真。

这条提示本身是错的：音频播放不占用网络传输通道，也不改 WiFi 配置，
WiFi 页的断开/重连/扫描/选网与它并不冲突。

## 修改

- `network.c` 新增并导出 `thingsos_network_wifi_busy()`，内容与原有
  `wifi_operation_busy()` 完全一致（`busy` 为真即忙；`external_busy` 仅在
  非音频会话时算忙）；`wifi_operation_busy()` 改为它的别名，避免两处判定漂移。
- `thingsos_network_radio()` 内联的同一条件也改为调用该判定。
- WiFi 页三个入口（「断开连接/再次连接」、「扫描/使用热点」、网络列表选择）
  由 `thingsos_network_busy()` 改为 `thingsos_network_wifi_busy()`。

未改动：真实传输（上传/OTA/管理任务）、微读与 ima 任务、USB 直连、文件删除、
自动休眠、阅读预绘等仍使用 `thingsos_network_busy()`，继续把音频占用视为
「不可抢占」，因为那些操作会争用同一份 SD/IO 所有权。传输页的「结束传输」
按钮同样保留原判定：传输自身占用当然要等它结束。

## 主机验证

新增 `device_ui_test` 回归（真实 tap 路径，`y=470` 即「断开连接/再次连接」）：

| 场景 | 期望 |
| --- | --- |
| 音频 `PLAYING` + 外部占用，已连接 | 断开成功，`host_wifi_disconnects+1`，提示不含「传输」 |
| 音频 `STOPPING`（停止在途，占用仍在） | 断开成功，提示不含「传输」 |
| 音频 `IDLE` + 真实传输占用 | 仍然拒绝，提示含「传输」 |

先临时把三处判定改回 `thingsos_network_busy()` 运行，该用例在
`device_ui_test.c` 第 2048 行失败；改回修复后 40/40 通过。因此这条测试确实
覆盖了用户报告的缺陷，而不是只覆盖新代码。

主机测试只证明判定与提示分支，不证明真机上的 WiFi 重连结果；实机仍应确认
播放音乐时点击「再次连接」能真正重新关联，以及传输进行时该按钮仍被拒绝。

## 设备验收

1. 播放一首音乐，进入 WiFi 页点击「断开连接」，再点击「再次连接」：
   应正常断开并重新连接，不出现「正在传输」。
2. 暂停或刚停止播放后立刻操作，同样不应出现该提示。
3. 反向确认：手机正在传书时，该按钮仍提示「正在传输，请完成后调整网络」。

## 已安装的设备证据（2026-10-30）

沿用本轮前一次烧录的用户授权，按同一脚本流程安装（备份 → 写主应用
`0x220000` → 写选择区 `0xf000` → 回读 → 保护分区比对 → 启动核对）。

- 备份 `before.bin` SHA-256
  `8b4920dcb4a22c8ddff314cd4b0eda4af9539afda0f29461fe25bb6d5bcb158b`。
- 候选 `0.0.1-v1-beta`，6,497,200 B，SHA-256
  `3f176fb7d008906e1c6820eb6bff0e91abbc165c6f9fc1b952e6659c6dcee1c6`；
  回读逐字节匹配，选择区 `seq=1`、`state=0` 且回读匹配。
- bootloader、分区表、PHY、NVS、救援分区、system_font 均与备份一致。
- 启动：`Loaded app from partition at offset 0x220000`、ELF
  `70063656b...`（与本地 ELF 一致）、`App version: 0.0.1-v1-beta`、
  `Black-to-black correction ready=1 phases=36`、`UI ready`、
  `Startup health confirmed`，无欠载、无 `E (` 错误。

`flash-result.json` 中 `physical_wifi_reconnect_verified` 仍为 false：
串口与主机测试只证明判定分支，音乐播放中真正重新关联、以及传输时仍被拒绝，
需要在设备上按上面三步确认。
