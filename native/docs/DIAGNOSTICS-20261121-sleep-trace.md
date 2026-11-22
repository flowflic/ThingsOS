# 休眠/唤醒复位追踪（2026-11-21）

背景：11-21 的诊断包显示，休眠中多次 `reset=5`（`ESP_RST_INT_WDT`）重启，表现为“自动唤醒”。
原日志复位后只剩最后一条事件，无法判断复位发生在进入休眠、休眠中、恢复中还是醒来之后。

本次只增加诊断，不改变休眠、唤醒和 panic 的行为；版本号不变。

## 实现

- `firmware/components/thingsos_board/sleep_trace.c`：RTC_NOINIT 结构（与诊断摘要 `retained` 同在
  `.rtc_noinit`），记录当前电源阶段、上一阶段、阶段时间、本次休眠后的 light sleep 次数、最后一次
  唤醒源与返回码。
- 通过 `-Wl,--wrap=esp_panic_handler` 在 IDF panic 处理前（IRAM、只做 RTC 内存写入）保存：异常类型、
  出错核、panic 时所处阶段、`mcause`/`mtval`，以及两颗核各自的 PC（mepc）与 RA。
- 打点位置：`thingsos_board_suspend` 开始/完成，`sleep_wait` 进入 light sleep 前/返回后、USB 插入时的
  空转，`thingsos_board_resume` 开始/完成，醒来后第一次成功刷屏（AWAKE）。
- 下次启动时，若上次为异常复位（看门狗、panic、掉电、锁死、未知），写入 `DIAG_TRACE` 事件后清零。

## 事件 `kind=18`（DIAG_TRACE）

| stage | a | b | code | errno |
|---|---|---|---|---|
| 0 | 复位时的阶段 | 上一阶段 | 最后一次 light sleep 返回码 | 阶段距本次启动的秒数（未知为 -1） |
| 1 | 本次休眠后的 light sleep 次数 | 最后唤醒源位图 | 复位原因 | 0 |
| 2 | panic 类型（1 IWDT、2 TWDT、3 abort、4 fault、0 debug） | 出错核 \| (panic 时阶段 << 8) | mcause | mtval |
| 3/4 | 核号 0/1 | 0 | 该核 PC | 该核 RA |

阶段编号：0 启动、1 开始休眠、2 休眠准备完成、3 进入 light sleep、4 light sleep 返回、5 USB 空转、
6 开始恢复、7 恢复完成、8 醒来后已刷屏。PC/RA 需要用对应版本的 ELF
（`addr2line -e ThingsOS_native.elf <PC>`）解析。stage 2～4 只在 panic 处理器实际运行时出现；
若硬件看门狗直接复位，只有 stage 0/1。

## 第二轮（2026-11-21 下午）：每核心跳与主循环细分

11-21 13:44/13:45 两次复位都没有 panic 记录（两核都未进入看门狗中断），阶段停在 7（恢复完成）
和 4（light sleep 返回）。为定位两核停在哪里，增加：

- **每核 tick 钩子**（`esp_register_freertos_tick_hook_for_cpu`）：每个 FreeRTOS tick 记录该核已服务
  的 tick 数，以及当前运行任务名前 8 字符。钩子在 IRAM、不调用任何函数（任务名读内核 DRAM 中的
  `pxCurrentTCBs` 与 `StaticTask_t` 镜像，仅当 TCB 在内部 RAM），不取内核锁。两核关中断或总线卡死时，
  这些值停在各自停下的时刻。
- **主循环细分打点**（`thingsos_board_trace_mark`），记录位置和当时核 0 的 tick 数。

新增事件（仅异常复位后）：

| stage | a | b | code | errno |
|---|---|---|---|---|
| 5 | 主循环位置 | 该位置时核 0 tick 数 | 核 0 最后 tick 数 | 核 1 最后 tick 数 |
| 6 | 核 0 任务名[0..3] | 核 0 任务名[4..7] | 核 1 任务名[0..3] | 核 1 任务名[4..7] |

任务名为 ASCII 小端打包（固件内部常量，不含用户数据）。tick 数自钩子注册（启动时）起各核分别计数，
light sleep 期间不增长；核 0 与核 1 之差可看出哪颗核先停，`code - b` 为停在该位置后核 0 又运行的 tick 数（ms）。

主循环位置：1 循环开始、2 配对轮询后、3 服务轮询后、4 等待 UI 锁、5 已取得 UI 锁、6 应用轮询后、
7 闹钟/阅读同步轮询后、8 电源键处理后、9 进入休眠等待；20 恢复时开 WiFi 前、21 开 WiFi 后、22 恢复界面完成；
30 开始绘制、31 开始刷屏、32 刷屏结束。

## 结论与修复（2026-11-21 晚）

- 诊断包 (5)：旧固件 boot 24 抓到 IWDT panic，核 0 PC `0x2f009890`、RA `0x400b50a2` 位于 S31 BLE 控制器库
  （`r_sym_ble_*`），核 1 空闲；新固件 boot 29/30/31 三次均为核 0 先停、核 1 约 800 ms 后停（核 1 在
  panic 处理中等待核 0），无 panic 记录。开机日志显示 BLE 控制器开机即启动并自动回连 2 个已保存设备，
  而 `thingsos_board_suspend` 只停 WiFi，`CONFIG_BT_CTRL_SLEEP_ENABLE` 未开启。
- 修复：`thingsos_ble_suspend()` 在息屏/关机前 `nimble_port_stop()` + `nimble_port_deinit()`（停主机与控制器），
  记录是否需要自动回连；唤醒后 `thingsos_ble_resume()` 重新初始化并回连上次设备。挂起期间不再安排回连。
  首次初始化尚未完成时（最多等 4.5 s）拒绝息屏并提示。主机测试 `ble_connection_pairing_and_gatt_lifecycle`
  覆盖挂起、唤醒回连和未开启自动回连三种情况。
- 休眠追踪诊断保留，用于确认修复后不再出现 `reset=5`。
