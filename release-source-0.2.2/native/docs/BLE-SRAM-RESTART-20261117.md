# 蓝牙搜索触发控制器内存分配断言

用户报告系统剩余 SRAM 约 33 KB，点击统一蓝牙列表的“搜索设备”后返回书库。使用不复位设备的串口采集，用户点击后在 `build/ble-sram-crash-20261117/repro-live.log` 捕获：

```
I (116048) BTDM_INIT: BTDM version [39ed4dc]
E (116049) BTDM_INIT: btdm_osal_elem_mempool_init failed: -1
assert .../btdm_hci_trans.c,52
assert failed: wr_btdm_assert btdm_log.c:111 (0)
ELF file SHA256: e9fb43eea
Rebooting...
rst:0xc (SW_CPU_RESET)
```

这是控制器初始化失败后的断言重启，并非界面主动返回。采集持续 600 秒，未人为复位，结束时关闭串口。该日志足以确认本次失败路径；不据此断言存在内存泄漏。

核对本机 ESP-IDF v6.1：S31 `bt.c` 的初始化首先创建 BTDM OSAL 内存池，失败后进入控制器清理；`wr_btdm_osal_malloc` 对内部池要求 `INTERNAL | DMA | 8BIT`。NimBLE 主机的外部内存选项不能替代控制器 DMA 内存；普通内部 SRAM 空闲统计也不能直接代表这个池可用的空间。

## 修复

- 使用 SDK 官方外部 BSS 方案，将其支持的 Wi-Fi、lwIP、BT 状态数据放入 PSRAM；经典蓝牙 UART 任务拥有的设备数组同样放入外部 BSS，供音频路由快速读取的可见快照仍留内部。
- 通过应用自己的链接片段将未修改的 Espressif NSNet3 档案中 `gtcrn_math` 的 BSS 放入 PSRAM。两张 8208 字节激活函数表仅由 `local_audio` CPU 推理访问；显示 ISR、LCD DMA 和控制器 DMA 缓冲不迁移。
- 关闭 Wi-Fi 可选 IRAM 加速驻留，静态 TX 缓冲从 12 个调整为 6 个，NVS 缓存采用 SDK 支持的 PSRAM 方案；保留 RX 缓冲、协议和 Wi-Fi/BLE 共存。此处以内部内存余量换取可选的 Wi-Fi 代码加速。
- BLE 初始化持有既有 I/O 锁检查内部 DMA 总空闲和连续块。低于 64 KiB / 24 KiB 时不进入本次已观察到的 SDK 断言路径，清除待处理请求、显示错误并允许重试；记录启动前后真实 DMA 与内部 SRAM 数值。
- 使用可检查返回值的主机任务创建，处理失败并释放已初始化的主机/控制器；SDK 原 void 包装器不检查任务创建失败。界面和显示仍由主任务拥有。

## 验证记录

70/70 主机回归通过；补充的蓝牙专项 3/3 通过，覆盖低 DMA 总量、碎片化、主机任务失败释放及随后正常重试。固件构建及 JPEG 链接检查通过。ELF 地址核对确认两张降噪表、网络与 UART 状态位于 `0x50000000` 外部 BSS 区域，音频路由快照仍在内部。相对故障 ELF，静态内部内存布局释放约 55.7 KiB；静态 TX 缓冲和 NVS 缓存还减少运行时内部占用。

候选及安装证据在 `build/ble-sram-crash-20261117/fix/`。BIN SHA-256 `e79011bf373c18beca032e6668b769700dc5294bcbb4d4697a36dd94e7553e4c`，ELF `2e716c32d14b89807bd51e0758209d409525c9f119b3d817389935895c557b42`，版本仍为 0.2.0。安装状态以 `usb-install/result.json` 为准，启动健康和真实搜索复测分别记录。

首次 `usb-install/` 基线捕获旧固件在 10164 ms 的真实屏幕欠载，保护门禁在任何写入前停止；随后 10 MHz 重绘恢复、完整 45 秒采集内健康确认。保留该记录后仅重做一次基线，未改变任何错误/ELF/槽位/哈希门禁。`usb-install-attempt2/` 基线无错误并完成实际救援启动、16 MiB 备份/回读、保护区/NVS/选择器及新应用 45 秒启动验证；串口关闭后由独立无复位采集接续复测日志。

已安装 ELF `2e716c32d...`。书库就绪时内部空闲 110659 字节、DMA 空闲 79587 字节；旧故障 ELF 同阶段内部空闲 38083 字节。用户再次点击搜索，实机日志确认 `BTDM controller init OK`，启动前 DMA 空闲 69367 字节，控制器/主机启动后内部空闲 63267 字节、DMA 空闲 32195 字节（此时临时初始化任务尚未退出）。该次搜索在用户点返回前已发现 20 个 BLE 设备，无原断言或重启。十分钟复测采集及用户反馈另存于 `retest/`；这不代表真实外设配对、连接及输入均已验收。
