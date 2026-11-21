# 微读联网省电策略与 Wi-Fi 发射功率对比（2026-11-02）

范围：只修改 Metalio 本树，版本保持 0.1.4，保留前一轮恢复的 b/g/n。没有修改发射功率、设备安装或发布。

后续状态：用户随后明确授权请求 12 dBm 上限，当前项目已增加该运行时限制，见 [限功率实现与验证](WIFI-TX-12DBM-20261102.md)。本说明下表与 `build/weidu-radio-20261102/` 记录的是限功率前的微读策略候选。

随后包含充电修复及限功率的 0.1.4 候选已按授权安装；实机微读任务读回省电模式 1→0→1，功率上限读回 48。该任务取消，未证实下载速度提升。见 [实机测试](CHARGING-WIFI-DEVICE-20261102.md)。

## 原有实现与本轮变化

本次核对 `weidu_device.c` 后确认：**登录、首次书架同步、正常下载、封面和阅读数据同步原本已调用 `radio_awake()`，结束时调用 `radio_restore()`**。前面讨论中“微读联网时默认仍开省电”的说法不准确，不能把这些场景原有的耗时都归于省电。

原实现的遗漏是 `WD_DEVICE_RESTORE`：它既有离线恢复正文，也有联网恢复正文后补齐划线/书评的路径，但两种都被排除在临时关闭省电之外。本轮让联网恢复也覆盖在任务级策略内。

- 所有微读网络任务（含登录、首次同步、下载、封面、划线/书评/进度、公众号、联网恢复）进入时请求 `WIFI_PS_NONE`。
- 从驱动读取任务前的真实省电模式，状态保存在本次 worker 的局部对象中；共同收尾路径恢复该值，涵盖成功、失败、取消。原本就是 NONE 时保持 NONE。
- 双预取线程由同一 coordinator 管理，下载退出前已等待线程结束；省电策略一直持有到整个 worker 收尾，不由每个线程分别修改，避免过早恢复。
- 纯离线恢复和退出账号不调整 Wi-Fi 省电。创建任务失败时尚未修改无线策略。
- 设置后与恢复后均回读省电模式；API 错误或模式不符会记录警告，不把设置请求成功当成生效证据。
- 保留原有切换后 100 ms 等待，不把删除等待当成未经测量的速度优化。

实现：`native/firmware/components/thingsos_weidu/radio_policy.inc` 与 `weidu_device.c`。

## 耗时与实机观测

新增日志：

```text
Radio exchange previous=1 set=0 actual=0 readback=0 tx_qdbm=<驱动值> tx_readback=0
Radio restore target=1 set=0 actual=1 readback=0
Exchange completed action=<任务> network=1 error=<结果> elapsed=<总耗时> ms
```

`actual=0` 是 `WIFI_PS_NONE`，`actual=1` 是 `WIFI_PS_MIN_MODEM`；`readback`/`tx_readback` 为 0 才说明对应读取成功。`tx_qdbm / 4` 是驱动报告的最大发射功率档位（dBm），不是 RF 仪表测得的每包输出。

原有 `Transport stage=... elapsed=... reused=...` 与 HTTP 响应字节记录继续保留，可区分目录、reader token、分片与会话请求。新总耗时包含目录/文件处理、缓存生成和调度；登录还包含扫码确认和服务器长轮询，不能直接当作网络吞吐。

本轮尚无新镜像的设备计时，**不能声称登录或普通下载已加速**；它们原来就已关闭省电。最直接可能获益的是此前遗漏的联网恢复分支，效果仍需同网络、同书籍、同缓存状态对照。应记录 DHCP、任务起止、实际省电模式、分阶段耗时和取消后的恢复结果。

## 发射功率区别

2026-11-02 重新查询三个仓库 HEAD，提交均与前一轮一致：官方 `690a023c6686`、kiikoread S31 `83942eb45d13`、dpReader `a41c9fe96993`。

| 固件 | 源码/配置 | 能确认的含义 |
| --- | --- | --- |
| ThingsOS 微读策略候选（限功率前） | `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=20`；当时应用与 BSP 无 `esp_wifi_set_max_tx_power()` 调用 | PHY 配置上限 20 dBm，未额外请求运行时下调；驱动值需设备回读，不能断言实际一直输出 20 dBm |
| Metalio 官方 | `kMetalioWifiMaxTxPower=48`，在 STA_START 应用；传输文档也声明 AP/STA 使用此上限 | API 请求 48 × 0.25 = **12 dBm** |
| kiikoread S31 | `TRANSFER_S31_MAX_TX_POWER=48`，radio profile 调 API | API 请求 **12 dBm** |
| dpReader31 | 同样为 `TRANSFER_S31_MAX_TX_POWER=48` | API 请求 **12 dBm**；日常省电与功率上限是分别设置的参数 |

**请求值不一定是最终档位。** 当前安装 SDK 的 `esp_wifi.h:1174` 和 [ESP32-S31 API 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/network/esp_wifi.html#_CPPv425esp_wifi_set_max_tx_power6int8_t) 明确列出量化映射：请求 `[44,51]` 对应驱动实际值 `44`，即约 **11 dBm 档位**。因此其他固件的 `48` 应表述为“请求 12 dBm，按该 SDK 文档会量化至约 11 dBm，具体镜像需回读确认”，不能直接称已测得 12 dBm 射频输出。

我们的 PHY 配置上限比它们的运行时请求高，但二者不是同层级的实测数据。实际功率还受 PHY 初始化、速率、信道/国家限制和校准影响。SDK 的 PHY 配置说明也注明高速率的实际发射功率可能更低。

更高发射功率不等于更快下载：它主要作用于设备发出的请求、ACK 等上行报文；微读正文下行还取决于路由器发送、接收质量、重传、服务端及存储。降低功率是否改善近距离稳定性或供电负载目前只是待验证方向。本轮不改这一变量。

来源：

- [官方常量](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/690a023c66862af103a3187464373ee7c5efcd14/components/esp-wifi-connect/include/wifi_station.h#L16)、[STA 启动应用](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/690a023c66862af103a3187464373ee7c5efcd14/components/esp-wifi-connect/wifi_station.cc#L428)。
- [kiikoread S31](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/83942eb45d13064a47aca7ff04c2faf8fd51f98a/components/read_pico_transfer/transfer_s31_radio_profile.h#L9)。
- [dpReader31](https://github.com/dephia31/dpReader31/blob/a41c9fe96993290052782dc767162f253a4ab26f/components/read_pico_transfer/transfer_s31_radio_profile.h#L9)。

本地上游摘录：`build/weidu-radio-20261102/tx-power-sources.json`。候选与构建/回归记录也放在该目录，b/g/n 基线镜像另存为 `baseline-bgn-ThingsOS_native.bin`，供后续实机对照。

## 验证范围

主机回归 `weidu_radio_awake_and_restore` 直接执行生产策略，覆盖所有微读 action、联网/离线恢复、NONE/MIN/MAX 的保存恢复、读取/设置失败、读回不一致和重复收尾；主固件编译检查真实 ESP-IDF API 集成。主机结果只验证控制逻辑，实际功率与性能需要安装后测量。

实际结果：完整主机回归 **46/46 通过**，主固件构建与 JPEG 链接检查通过，应用描述符为 **0.1.4**。候选镜像、基线哈希与验证结果见 `build/weidu-radio-20261102/result.json`。救援不含微读组件，未因本轮修改重建；沿用上一轮已编译的 b/g/n 救援镜像。未刷机、未发布，未声称速度改善或取得设备当前发射功率回读。
