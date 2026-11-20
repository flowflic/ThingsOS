# 系统应用 UE 原型

用户要求先确认 UE 再实现。本目录仅为可交互原型，未修改、编译或烧录任何固件。

入口为 `System Monitor UE.html`。浏览器从 `#apps` 查看应用入口，从 `#system` 查看监控页。应用页增加第三行左侧“系统”图标，原有入口和顺序保持。监控页提供 CPU 频率、CPU 使用率、SRAM、PSRAM、Flash 五行横向重叠占用条，黑色表示当前/已用，浅灰底条表示上限/总量。

原型按 684×1216 比例制作，参考当前 `apps_controls.inc` 与真实控制器绘制的 `ui-apps-icons-prototype.pgm`；参考截图仅用于复用原应用页、状态栏和导航栏。所有运行占用为模拟，切换阅读、传输和内存紧张场景可检查边界；每两秒自动模拟刷新可在页面外暂停。页面内返回、刷新和底部应用入口均可操作。

统计口径供本轮 UE 确认：

- CPU 频率：当前 MHz / 配置上限。当前构建默认配置为 320 MHz；不是 CPU 使用率。
- CPU 使用率：双核平均百分比，原型演示两秒窗口。真实采样方案待确认 UE 后实现。
- SRAM / PSRAM：可分配堆的已用、可用与总量。SRAM 384 KiB 为演示值，不能当作芯片物理 SRAM 规格；16 MiB PSRAM 来自既有启动日志，但实际可分配堆总量仍以实现时运行接口为准。
- Flash：整片 16 MiB。演示占用 11.71 MiB 包含当前主固件约 6.59 MiB 与其他分区保留约 5.12 MiB；4.29 MiB 为主应用槽升级余量，不代表可保存用户书籍的空间，不含 SD 卡。该口径需随 UE 一起确认。

原始来源与上下文：`native/firmware/main/apps_controls.inc`、`build/native-idf/config/sdkconfig.h`、`build/reader-transfer-readtime-20261114/validation.json`、`build/readtime-toggle-20261113/usb-install-20261113-retry1/device-install/main-boot.log`。不使用新的设计系统或外部网络资产。
