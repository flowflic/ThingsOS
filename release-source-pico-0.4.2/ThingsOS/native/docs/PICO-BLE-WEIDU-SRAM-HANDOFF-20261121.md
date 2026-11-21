# 小纸 Pico 蓝牙与微读 SRAM 优化交接

2026-11-21 · ThingsOS 0.4.1 · Read Pico RDP-G01-W · ESP32-S3、8 MiB PSRAM、16 MiB Flash

## 接手入口

在 `D:\crossmux\ThingsOS\native` 上继续。先按 P0 补齐实机基线，再分别试验 **蓝牙控制器活动槽 3 → 2** 和 **微读栈上临时缓冲迁往 PSRAM**。本文件中的优化项均待实施。下面的实测数据来自已经烧录并通过启动自检的内存诊断版。

仓库 `HEAD` 为 `6b49c9a`，实测固件包含位于工作区中的后续改动。接手时先检查工作区、构建清单和设备实际固件。实测主程序 SHA‑256 为 `b0f4c1bcc482a54c7127ab4b1681765b215efd26f1e8f688f68539d5db374f29`，保留副本在 `build/pico-memtrace-20261121/main-candidate.bin`。[烧录记录](../../build/pico-memtrace-20261121/installation.json) · [构建验证](../../build/pico-memtrace-20261121/candidate-validation.json)

## 实测基线

下表为堆的空闲量。DMA 空闲是内部 SRAM 中具备相应分配能力的子集。负载低点来自定期采样和事件采样，可能漏掉采样间隔内的瞬时低点。

| 场景 | SRAM 空闲 | SRAM 最大连续块 | DMA 空闲 | PSRAM 空闲 |
|---|---:|---:|---:|---:|
| Wi‑Fi 已连接，蓝牙未启动 | 76.4 KiB | 62.0 KiB | 68.9 KiB | 4.11 MiB |
| 蓝牙翻页器已连接 | 39.5 KiB | 23.0 KiB | 32.0 KiB | 4.01 MiB |
| 蓝牙连接时打开本地 EPUB | 39.2 KiB | 23.0 KiB | 31.7 KiB | 3.68 MiB |
| 蓝牙连接时开启传输 | 24.8 KiB | 11.0 KiB | 17.3 KiB | 2.55 MiB |
| 结束传输后 | 39.0 KiB | 23.0 KiB | 31.5 KiB | 2.55 MiB |
| 在线章节加载期间的 SRAM 最低采样点 | 17.4 KiB | 7.0 KiB | 9.9 KiB | 3.08 MiB |
| 整本缓存期间的 SRAM 最低采样点 | 15.6 KiB | 7.0 KiB | 8.1 KiB | 3.16 MiB |
| 整本缓存退出后 | 38.9 KiB | 23.0 KiB | 31.5 KiB | 3.52 MiB |

- 共采到 732 个样本。独立蓝牙扫描阶段缺样。蓝牙连接后比 Wi‑Fi 单开少约 37 KiB 空闲 SRAM。这是两个运行状态的差值，需要进一步分解其中的控制器、任务栈和其他分配。[内存 CSV](../../build/pico-memtrace-20261121/memory.csv) · [采样汇总](../../build/pico-memtrace-20261121/memory-summary.json)
- 蓝牙保持连接、传输关闭时，重登后验证了在线章节加载。两次复测各完成 3/3 章。整本缓存完成 134/134 章，耗时约 17 分 31 秒，任务栈最小剩余 4,276 字节。此前两次章节请求返回 `WD_AUTH`，本次完整采样未记录微读内存拒绝事件。[微读串口日志](../../build/pico-memtrace-20261121/weidu-serial.log)
- 微读章节加载、整本缓存等动作的启动门槛为内部 SRAM 空闲 **34 KiB**、最大连续块 **17 KiB**，以及 PSRAM 空闲 **576 KiB**、最大连续块 **64 KiB**。后台任务另有 DMA 余量检查，要求至少 **24 KiB** 空闲和 **8 KiB** 连续块。启动门槛在分配任务栈之前检查。实测蓝牙连接时仅高出 SRAM 总空闲门槛约 5 KiB，传输常驻时会降到门槛以下。[启动门槛](../src/weidu_reading.c#L1835) · [DMA 余量检查](../firmware/components/thingsos_weidu/weidu_device.c#L1199)

## P0 补齐测量

- [ ] 用原诊断版补采 **控制器初始化前后、扫描中、扫描结束未连接、HID 已连接、断开重连、关闭后**。区分主机任务启动时的短时开销与初始化任务退出后的稳态值。记录 SRAM、最大连续块、DMA、PSRAM 和蓝牙任务栈最小余量。
- [ ] 每个候选重复同一序列，覆盖无线传输启停、翻页器接收按键、本地 EPUB、在线未缓存章节、带图整本缓存、取消后重试。微读前记录传输开关、实际 HID 连接状态和启动前余量。运行中记录最低采样值，退出后记录回收量。
- [ ] 重复开关蓝牙、传输和微读若干轮。观察空闲总量能否回到基线，以及最大连续块是否逐轮变小。候选对照用相同 Wi‑Fi、外设和书籍，并标明预缓存状态。第一轮建议目标是 **Wi‑Fi 与蓝牙已连接、传输关闭、微读启动前至少有 45 KiB SRAM 空闲和 20 KiB 连续块**。这是优化目标，当前启动门槛见上节。

采样走 USB 串口，实测端口为 COM4，波特率 115200。采集程序按 USB 设备身份匹配端口，只读监听。固件在 USB 连接期间约每 2 秒采样，并在关键事件处追加快照。`usb-sample` 的 `subject` 是页面编号，`result` 是状态位，其中 `1` 为书籍打开，`2` 为请求开启传输，`4` 为微读忙，`8` 为蓝牙启用，`16` 为蓝牙连接，`32` 为休眠。`ble-state` 的 `result` 中，`1` 为扫描、`2` 为连接、`4` 为 HID 就绪。`weidu-worker` 和 `weidu-finished` 的 `subject` 中，`2` 代表整本缓存，`17` 代表章节请求。采集脚本与数据都在本机 `build` 下，交接到其他机器时一并带上必要的日志和构建清单。[采样格式](../include/thingsos/memory_trace.h) · [USB 周期采样](../firmware/main/main.c#L3748) · [采集脚本](../../build/capture-pico-memory-20261121.py)

## P1 优化蓝牙和微读

### 蓝牙

- [ ] **活动槽 3 → 2 独立 A/B。** `CONFIG_BT_CTRL_BLE_MAX_ACT=3`，NimBLE 已配置最多一个并发连接。ESP-IDF 标注每减少一个活动槽约省 828 字节，实机收益待测。覆盖搜索新设备、已连接时搜索、首次配对、断线自动重连、连续按键和 Wi‑Fi 并行使用。构建验证目前固定检查活动槽为 3。候选分支同步核对有效配置与验证规则，保留对实际活动槽数量的验证。[固件配置](../firmware/sdkconfig.defaults#L18) · [构建校验](../verify_build.py#L143) · [ESP-IDF 活动槽说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/kconfig-reference.html#config-bt-ctrl-ble-max-act)
- [ ] **空闲回收控制器。** 蓝牙已启用但无连接时，在扫描、配对和自动重连周期结束后，评估复用 `thingsos_ble_stop()` 释放控制器。明确重新搜索、手动连接和自动重连如何重新启动控制器。保留已连接翻页器的正常使用及现有配对记录，检查停止与重新启动重叠时的状态和队列处理。[蓝牙控制器实现](../firmware/components/thingsos_ble/ble_scan.c#L221) · [关闭路径](../firmware/components/thingsos_ble/ble_scan.c#L287)
- [ ] **核算包池和任务栈。** NimBLE 主机动态分配已优先使用 PSRAM。先分清控制器的内部占用、主机栈以及初始化和关闭任务的短时占用。结合扫描和配对压力，再评估 ACL、事件包池及任务栈的实际可减量。以掉键、扫描缺设备和配对失败作为回归信号。[NimBLE 内存配置](../firmware/sdkconfig.defaults#L21)

### 微读

- [ ] **搬临时缓冲，再试 16 → 14 KiB。** 当前任务栈为 16 KiB，构建产物的 `.su` 报告显示 `worker` 和 `fetch` 的单函数栈帧约为 2,480 和 1,536 字节。检查 URL、路径、请求和响应临时数据的生命周期，把适合的缓冲迁到可复用的 PSRAM 工作区，保留会跨回调或 SDK 调用使用的缓冲直至请求结束。用运行时栈最小余量决定能否将任务栈降到 14 KiB，直接目标是节省 2 KiB 内部 SRAM 并降低所需连续块大小。[微读请求入口](../firmware/components/thingsos_weidu/weidu_device.c#L303) · [任务创建](../firmware/components/thingsos_weidu/weidu_device.c#L1256) · [Espressif 栈用量测量](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/ram-usage.html)
- [ ] **覆盖栈的重路径。** 测登录和重登、目录请求超时重试、在线章节及预取、带图整本缓存和 EPUB 打包、取消重试、进度和阅读时长补传。134 章缓存结束时的 4,276 字节剩余栈只证明该流程。测到最拥挤的路径后，再根据实际栈需求及 DMA 余量复核启动门槛。
- [ ] **减少任务叠加。** 章节加载与整本缓存期间延后非紧急封面预取和后台同步，完成后恢复；复用现有联网任务互斥与取消机制。传输结束后曾回收约 14.2 KiB SRAM。如需处理传输常驻与微读的抢占，可另测保留手动常驻选项的空闲回收方案。传输的 12 KiB 服务栈和上传路径的 4 KiB 栈上缓冲可另开任务测量。[传输服务栈](../firmware/components/thingsos_net/network.c#L1002) · [上传缓冲](../firmware/components/thingsos_net/network.c#L841)

## P2 单独评估高收益方案

- [ ] **蓝牙控制器 Flash 运行模式。** 单独试编 `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY`，比较链接映射的 IRAM、内部堆起始地址及实机 SRAM。ESP-IDF 文档标注该选项可节省超过 20 KB，Pico 收益待测。当前固件未启用 Flash 自动挂起，串口仅将板上 Flash 识别为 `generic`。先核实芯片和挂起支持，再测试配对保存、NVS 写入、Flash 擦除及升级期间的 HID 延迟、掉键和掉线。[Flash 运行配置](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/kconfig-reference.html#config-bt-ctrl-run-in-flash-only)
- [ ] **拆分微读任务。** 若 P1 收益不足，列出微读联网、解析、打包和 NVS/Flash 写入的调用边界。让纯网络与普通数据处理任务显式使用 PSRAM 栈，把会关闭缓存的 Flash/NVS 操作留给内部 SRAM 栈的任务。审计 SD/DMA、取消、任务退出、互斥锁和错误回收后再迁移，保留现有配对、登录、阅读进度和缓存数据。[微读 NVS 路径](../firmware/components/thingsos_weidu/weidu_device.c#L127) · [ESP32-S3 外部 RAM 限制](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/external-ram.html)

## 验收与交付

- [ ] 每轮记录构建参数、主程序哈希、链接映射差值、主机测试和实机采样。沿用现有主机测试及 `native/verify_build.py`。实测基线通过了 47 项主机测试、烧录回读与启动自检。[构建验证脚本](../verify_build.py)
- [ ] 实机检查翻页器配对、扫描、断线重连、连续按键；蓝牙连接时的章节、整本缓存、取消重试、内存回收；传输可启动结束；屏幕刷新、欠载次数和 SD 读写。记录是否通过及对应日志。
- [ ] 保留 Pico 的 18 MHz PCLK 与 12 MHz 欠载回退、面板 DMA 预留、工厂 VCOM、分区与救援路径。取得当前候选的烧录授权后，核对设备身份，备份 Flash，先验证旧救援启动，再更新主槽、回读比对并检查启动自检。主机通过、烧录通过和实机功能通过各自记录。[工程约束](../../AGENTS.md) · [上次安装记录](../../build/pico-memtrace-20261121/installation.json)
