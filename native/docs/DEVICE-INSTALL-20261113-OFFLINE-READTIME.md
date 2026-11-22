# Metalio 离线阅读时长候选安装记录

2026-11-13，用户明确授权“烧录到设备”。

设备：Metalio E-Ink4-Plus / ESP32-S31 / 16 MiB，USB 序列号 `1C:29:04:D0:4A:B0`，当次 COM5。

版本保持 0.1.7，主程序 6,847,664 字节。

- 主镜像 SHA-256：`19a28c84969100cb9eefc21201a387572396c1ec07e26857b039e7276eb73e6f`
- ELF SHA-256：`49437661c3b0fcd91877b6099ac49a8b946f705a2c04920ffb8444d9fbe137cc`
- 完整 Flash 备份 SHA-256：`1e1c1964e5d92378888bcc26f39153a968e2ac44d47359b866983b028e2ea470`

按 `METALIO-USB-FLASH-PROCEDURE.md` 执行并通过：原主程序启动与 45 秒健康观察、完整 16 MiB 备份、实际启动并观察救援 45 秒、救援后 NVS 快照、仅写主程序 0x220000、完整 16 MiB 回读、保护区和 NVS 对比、最后才写入并回读启动选择器、新主程序启动观察 45 秒。

主程序逐字节回读一致；bootloader、分区表、PHY、保留区、救援及系统字体均保持一致。NVS 与救援后快照和原始备份均一致。没有更新救援、清空配置或写整片合并包。

新应用实际启动证据：

```text
I (1829) app_init: App version:      0.1.7
I (1830) app_init: ELF file SHA256:  49437661c...
I (6491) ThingsOS: UI ready: library; initial screen update succeeded; stack margin=18484 bytes
I (36498) ThingsOS: Startup health confirmed: display, font, touch; single app, no version rollback
```

未发现所检查的 panic、断言、无效段、面板绘制失败或 underrun。串口已关闭，设备已交回。

这只确认安装、镜像完整性和基本启动健康。尚未使用真实微信读书账号验证离线时长入账、授权续期、跨日、多设备位置或异常网络恢复。首次使用需在“微读 → 同步 → 离线阅读时长补传”扫描同账号授权二维码并启用，重新打开微读书籍后生效。建议离线阅读两到三分钟、关书、重启，再联网核对待发队列与微信读书实际时长。

全部原始备份及日志只保存在本机 `build/offline-readtime-20261113/usb-install-20261113/device-install/`，不发布或提交。验收清单：`build/offline-readtime-20261113/usb-install-20261113/DEVICE-ACCEPTANCE.md`。验证结果：`build/offline-readtime-20261113/validation.json`。
