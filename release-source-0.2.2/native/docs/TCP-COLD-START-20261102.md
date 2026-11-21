# Metalio 开机 TCP 停滞定位与上游对比（2026-11-02）

本轮只读源码、复核已有设备日志并查询公开上游，没有刷机、重启设备或进行新的固件实机 A/B。固件版本仍为 0.1.4。

后续操作（2026-11-02）：用户授权先恢复 b/g/n，已在 `network.c` 的 STA 协议掩码加入 `WIFI_PROTOCOL_11N`；省电、功率与缓冲配置保持原值。下文 ThingsOS 的 b/g 数值指调查基线，旧实机日志也属于基线，不能作为新候选的验证结果。

该单变量候选的主/救援固件均已构建通过，应用描述符保持 0.1.4，主固件 JPEG 链接检查通过。基线镜像、候选镜像、构建日志与 SHA-256 记录保存在 `build/network-bgn-20261102/`；未刷机、未发布，TCP 冷启动效果待实机验证。

## 结论与置信度

**确认存在 DHCP 完成后的 TCP 建连停滞。定位范围已收敛到 TCP 建连收发路径，尚未定位到具体驱动函数或网络丢包点。** 裸 IPv4 TCP 也失败，因此 DNS、TLS 证书、HTTPS 服务器业务以及更新清单解析不能解释全部现象。

当前最值得做的单变量实验是恢复 **802.11 b/g/n**：ThingsOS 现有代码固定为 b/g，而 Metalio 官方、kiikoread S31、dpReader 均保留 b/g/n。这是源码差异与实验优先级，**不是已经证明 b/g 是根因**。旧日志没有覆盖 b/g 与 b/g/n 的同条件冷启动对照。

**不能把省电单独定为根因。** 已有 `ps=0` 样本在 DHCP 后仍约 30.9 秒才首次观察到 TCP 成功；dpReader 的日常 profile 也保留 `WIFI_PS_MIN_MODEM`。官方和 kiikoread 使用 `WIFI_PS_NONE`，适合作为后续对照的第二个变量。

**NTP 与 TCP 不是直接依赖关系。** NTP 使用 UDP/123；HTTP Date 兜底才依赖 TCP。旧样本也显示 UDP/53 有回复时 UDP/123 仍可能没有回复。时间未显示还叠加状态栏刷新周期，不能用左上角出现时间的时刻代替 TCP 恢复时刻。

## 1. 重算已有实机证据

统一使用设备日志中的毫秒时间戳，从该次启动第一次 DHCP 完成开始计算。忽略最终 ROM reset 标记之前的上次启动尾部。**首次观察到成功只是采样上界，不是精确恢复时刻，也不是“需要预热这么久”的因果证据。**

| 归档日志 | DHCP 完成（开机后） | 首次观察到 TCP 成功（开机后） | DHCP 至该成功 | 成功前 UDP/53 回复 |
| --- | ---: | ---: | ---: | --- |
| [cold-probe-v1.log](../../build/update-check-20261101/cold-probe-v1.log#L326) | 7.907 s | 33.464 s | 25.557 s | 14 ms |
| [cold-probe-v2.log](../../build/update-check-20261101/cold-probe-v2.log#L208) | 7.957 s | 65.674 s | 57.717 s | 16 / 13 ms |
| [inbound-probe.log](../../build/update-check-20261101/inbound-probe.log#L370) | 7.856 s | 38.835 s | 30.979 s | 早期未采样，不能断言 |
| [ps-none-probe.log](../../build/update-check-20261101/ps-none-probe.log#L1262) | 54.463 s | 85.348 s | 30.885 s | 13 ms |
| [hotspot-probe.log](../../build/update-check-20261101/hotspot-probe.log#L266) | 7.883 s | 93.397 s | 85.514 s | 146 ms |

重现分析及包含原日志 SHA-256、原始行号、超时列表的结果：

```powershell
python build/network-cold-start-20261102/analyze_logs.py --output build/network-cold-start-20261102/log-evidence.json
```

### 足以支持的判断

- `cold-probe-v2.log` 中局域网 `192.168.31.30:9000` 与公网 `223.5.5.5:443` 均多次 `select=0`，每次约 2 秒；期间公网 UDP DNS 有正常往返。这把问题从“官网第一次 HTTPS 慢”缩小到了更底层的 TCP 建连路径。
- 同一日志约 13.546–58.627 秒内 `tcp.xmit` 从 4 增至 24，`tcp.recv=0`、`tcp.chkerr=0`；65.676 秒时首次成功后 `tcp.recv=1`。本地 SDK 的 `tcp_in.c:146` 在 TCP 校验和检查之前增加 `tcp.recv`，所以这段故障中看不到进入 `tcp_input()` 的 TCP 包。**仍不能区分 SYN 没有真正发出、对端没有回应、回复被 Wi-Fi/上层 IP 路径丢弃。** `tcp.xmit` 也不证明空口发送成功。
- 手机热点样本实际取得 `172.20.10.6 / gateway 172.20.10.1`，公网 TCP 同样早期失败、后续成功，降低了“小米路由器独有故障”的优先级。该样本仍探测了写死的 `192.168.31.1`；这些旧网关探测全部排除，不能拿来评价手机热点的网关连通性。
- `ps-none-probe.log` 明确记录 `ps=0`，仍有约 31 秒的 DHCP 后建连停滞。它没有相同条件的多次重复，足以否定“关省电就一定消失”，不足以评价省电对发生率的影响。

### 旧探测的误判与局限

1. 非阻塞 `connect()` 初次返回 `-1 / errno=119(EINPROGRESS)` 是正常行为；最终 `select>0 && getsockopt(SO_ERROR)==0` 才表示建连完成。部分日志打印的 `r/errno` 保留初次调用结果，不能只看它们判断失败。当前发布用的 `remote_update.inc:warm_connect()` 已按最终结果判断。
2. `select=0, so=0` 代表等待超时，不能视为成功。`ps-none-probe.log:1186` 的 `warm first tcp ok at 50 s` 紧跟一个 `select=0`，属于旧诊断的错误成功标记；本次重算不采用该标记。
3. `pc_pair.py` 的 UDP 分支只记收到报文，**没有发送回包**。设备端对 `9001` 的 `recv=-1` 无法证明 UDP 网络故障。PC 日志与设备日志起点也不同，未经时钟对齐不能直接相减。
4. `pc_inbound.py` 把 `connect()` 与 `recv()` 放在同一个 try 块，只记合并错误。早期 `TimeoutError` 无法区分握手超时还是服务器应用响应超时，不能据此证明收发两个方向都在 SYN 阶段阻塞。
5. 开机 30 秒的 `startup_healthy` 是显示、字体、触摸健康确认，没有解锁 TCP 的代码。lwIP 的 `sys_now()` 使用 FreeRTOS 单调 tick，不能用“还未 NTP 校时”解释 TCP 定时器失效。

## 2. 同芯片上游源码对比

本次查询固定到下列提交，避免把本地 10 月参考快照误当当前上游。参考源码只用于研究，没有导入第三方应用代码。

| 项目 | 提交 | STA 协议 | 省电策略 | TX 功率请求上限 | TCP 接收窗口 / 邮箱 |
| --- | --- | --- | --- | --- | --- |
| ThingsOS 0.1.4 调查基线 | `7745885` + 当时配置 | **b/g** | 默认 MIN_MODEM；上传时临时 NONE | 未显式限制，使用 SDK 策略 | 5760 B / 6 |
| Metalio 官方 2.0.71 | `690a023c6686` | **b/g/n** | **NONE**；SetPowerSaveMode 也保持 NONE | 48 × 0.25 dBm = 12 dBm | 32768 B / 32 |
| kiikoread S31 | `83942eb45d13` | **b/g/n** | **NONE** | 12 dBm | defaults: 65535 B / 64 |
| dpReader31 | `a41c9fe96993` | **b/g/n** | 日常 **MIN_MODEM**；校时时临时 NONE | 12 dBm | defaults: 32768 B / 32 |

官方数值取已提交 `sdkconfig`，第三方取 `boards/metalio47/sdkconfig.defaults`；实际第三方构建可能被生成配置或批量传输 profile 覆盖，不能当成本次实测。dpReader 基于 kiikoread，两者不是独立实现样本。

关键来源：

- [官方 Wi-Fi 初始化与 profile](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/690a023c66862af103a3187464373ee7c5efcd14/components/esp-wifi-connect/wifi_station.cc#L35)、[官方网络与传输配置说明](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/690a023c66862af103a3187464373ee7c5efcd14/docs/ap-file-transfer.md#12-s31-无线配置)、[官方 sdkconfig](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/690a023c66862af103a3187464373ee7c5efcd14/sdkconfig)。
- 官方在 [2026-10-23 的 2.0.66 提交](https://github.com/CloudZao/Metalio-E-INK4-Plus/commit/2c1e5efba74f9ffd6f7d72e348011308e53f39a1) 中加入 b/g/n + NONE、启动/暂停生命周期锁和信号优先的 AP 选择。这个提交说明它主动调整过网络策略，但没有提供本问题的同条件复测，不能宣称是同一故障的已证实修复。
- [kiikoread S31 radio profile](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/83942eb45d13064a47aca7ff04c2faf8fd51f98a/components/read_pico_transfer/transfer_s31_radio_profile.h#L11)、[配置](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/83942eb45d13064a47aca7ff04c2faf8fd51f98a/boards/metalio47/sdkconfig.defaults)。
- [dpReader radio profile](https://github.com/dephia31/dpReader31/blob/a41c9fe96993290052782dc767162f253a4ab26f/components/read_pico_transfer/transfer_s31_radio_profile.h#L11)、[配置](https://github.com/dephia31/dpReader31/blob/a41c9fe96993290052782dc767162f253a4ab26f/boards/metalio47/sdkconfig.defaults)。

其他差异：官方静态 RX/TX 为 6/6、cache TX=4，ThingsOS 为 8/12、cache TX=32；官方 `SPIRAM_MALLOC_ALWAYSINTERNAL=16`，ThingsOS 为 16384；ThingsOS 开启 L2→L3 包复制，官方关闭。它们值得在第二阶段检查，但没有证据把某个缓冲参数定为冷启动根因。TCP 窗口和 `TCP_NODELAY` 主要影响握手后的数据流，当前证据不支持先扩大窗口来解决 SYN 阶段停滞。

排除主比较对象：Metalio-WhiteAI 的 README 明确是 **ESP32-S3 / Metalio E-Ink 4**，并非 Plus/S31；Read Pico 官方及 CrossMux 的 S3 实现也只能作架构参考，不能据其正常联网断言 S31 硬件或 SDK 正常。

上游摘录与行号保存在 [upstream-evidence.json](../../build/network-cold-start-20261102/upstream-evidence.json)。本轮没有官方或第三方镜像在同一台设备、同一 AP 的新冷启动数据，因此**不声称它们已经实测不存在此问题**。

后续核对补充：本树微读登录及普通下载原本就临时关闭省电，遗漏的是联网恢复缓存后的补数据分支。其他固件传入的 `48` 是名义 12 dBm 请求值；本 SDK 文档将其量化到约 11 dBm 档位。详见 [微读省电策略与功率区别](WEREAD-WIFI-POLICY-AND-TX-POWER-20261102.md)，不能把表中请求值当作射频实测。

## 3. 校时与日期显示是三层问题

### UDP/123 的收包与轮询

`network.c:424` 在拿 IP 后启动 SNTP，三个源依次为 `203.107.6.88`、`ntp.aliyun.com`、`cn.pool.ntp.org`。配置允许最多 5 秒启动随机延迟；本地 lwIP `sntp_opts.h:164` 默认接收超时 15 秒，失败后有切源/退避。没有成功/失败逐包轨迹时，不能把最终 NTP 成功的延迟直接归因为 TCP。

旧 [time-sync-diag4 main-boot.log](../../build/time-sync-diag4-20261031/device-install/main-boot.log#L302) 中，对两个固定 NTP 地址的 UDP/123 探测没有回复，切换省电前后均如此，而 `8.8.8.8:53` 收到 12 字节回复。这仅证明当时测试的 UDP/123 请求/回复路径有问题；仍不足以判定网络封端口、服务端拒绝还是设备丢包。

[kiikoread 的独立 NTP 实现](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/83942eb45d13064a47aca7ff04c2faf8fd51f98a/components/read_pico_transfer/transfer_ntp_time.c#L14) 按源进行最多三次约 2 秒等待，且校时期间关闭省电。它提供更清晰的单次尝试边界，不证明当前网络的 UDP/123 一定可达。

### TCP 相关的是 HTTP Date 兜底

`network.c:263` 首次在 SNTP 启动后 10 秒触发 HTTP Date，未得到有效时间时每 30 秒再试。每轮访问百度、阿里云，各设置 5 秒 HTTP 客户端超时。TCP 建连停滞直接影响这一条兜底；连通恢复后也可能还要等待下一轮调度。现有预热仅探测发布主机 443，不会触发 SNTP 立即重试，也不能证明预热缩短了底层故障。

### 已校时到屏幕显示、RTC 保存

`main.c:1899` 只在系统时间有效时绘制日期，`main.c:3965` 的空闲状态栏更新与 RTC 回写约每 60 秒一次。SNTP 回调只设置来源待保存标志，没有直接通知 UI 立即刷新。因此静止页面在校时后仍可能等到下一次分钟周期；用户交互引发重画时可能更早显示。系统时间、屏幕显示、RTC 写入必须分别记录。

本树开机也会从有效 PCF8563 读取时间（`main.c:3461`）。如果 RTC 仍有效，重启后并不必然需要重新等 NTP 才显示日期。持续空白还应核对 RTC VL、I2C 读写结果及是否在回写周期前重启。

官方同样区分 RTC 恢复和网络取时：[当前 RTC 初始化源码](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/690a023c66862af103a3187464373ee7c5efcd14/main/boards/metalio-e-ink4-plus/periph/metalio_rtc.cc#L54) 不等待网络；其 [开源版新机无法取时 issue](https://github.com/CloudZao/Metalio-E-INK4-Plus/issues/3) 报告的是无效 RTC 与空云端地址的组合，不能当成本问题的 TCP 故障报告或修复证据。

## 4. 最小验证路径

根因闭环需要同一设备的冷启动收发轨迹；只继续缩短超时或增加预热不能回答包丢在哪里。

1. **先只恢复 b/g/n**，保留省电、TX 功率、内存与 TCP 配置，比较当前 b/g 基线。每组至少 5 次，从 DHCP 开始计时，分别记录 LAN 和 WAN 首次真实握手成功时间。使用局域网受控 TCP 服务，公网结果作为补充。
2. 其次在 b/g/n 上只关闭省电；如仍复现，再单独对照官方 12 dBm 请求上限。三个变量不同时修改，避免成功后仍不知道哪个变量生效。
3. LAN 服务同时抓包，区分：没有看到设备 SYN；看到 SYN 但没有 SYN-ACK；发出 SYN-ACK 后设备仍重传；三次握手完成但应用未返回。设备侧记录 lwIP 出口、Wi-Fi TX 完成和 RX→IP→TCP 入口；PC 没看到 SYN 时还需路由器或空口侧证据，不能直接归咎设备。
4. 每轮 UDP 同时使用真实 DNS 查询、48 字节 NTP 请求及可返回报文的 LAN echo。服务端均返回唯一轮次 ID，日志同时记 UTC 与 monotonic、地址/端口、errno、最终 SO_ERROR。旧 PC echo 缺口应先修正。
5. 若 b/g/n 仍失败，用同 SDK 的最小 STA/socket 工程分离应用影响，再对比官方/第三方固件。记录 SDK commit、Wi-Fi 库标识及芯片 revision；相同“6.1”字样不足以确认库版本相同。未经设备安装授权，本轮不执行这些固件切换。

进一步分类：b/g/n 单变量稳定消除故障，才支持 legacy PHY 路径相关；最小工程仍复现则继续查 SDK/驱动/网络，最小工程正常才回头查应用生命周期、缓存和内存。校时成功后 UI 通知与 RTC 回写可独立改进，但不应计作 TCP 根因修复。
