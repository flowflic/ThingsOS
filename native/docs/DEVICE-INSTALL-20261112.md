# Metalio 0.1.7 开发候选安装记录

2026-11-12，用户明确授权“烧录到设备，并告诉我要验证哪些内容”。

设备：Metalio E-Ink4-Plus / ESP32-S31 / 16 MiB，USB 序列号 `1C:29:04:D0:4A:B0`，当次串口 COM5。

安装候选为 `build/image-hold-20261112/ThingsOS_native.bin`，6824448 字节，版本保持 0.1.7。

- 主镜像 SHA-256：`abf9e2822ddeccca4771381170aad9a8510583ac58834ea487323d26267b9a9e`
- ELF SHA-256：`f4b594ecc7cbc1a803a5dba2088ad258f696870907abe3c08334469d266c94a1`
- 完整 Flash 备份 SHA-256：`b7357e12cf669611178ab25d8b391cdb7b511ef4abfbe9cb9a9c11d135de26df`

按 `METALIO-USB-FLASH-PROCEDURE.md` 执行：基线主应用实际启动与健康确认、完整 16 MiB 备份、旧救援实际启动并观察 45 秒、救援后 NVS 快照、仅写主应用 `0x220000`、完整 16 MiB 回读、保护区域与 NVS 比较，然后才写入并回读启动选择器。

本次所有门禁通过。引导、分区表、PHY、保留区、救援、系统字体保持一致；NVS 与救援后快照及初始备份均一致。没有更换救援镜像、清空用户配置、写完整 USB 合并包或更改面板/充电参数。

新主应用启动日志确认：

```text
App version:      0.1.7
ELF file SHA256:  f4b594ecc...
UI ready: library; initial screen update succeeded
Startup health confirmed: display, font, touch
```

启动健康在 36585 ms 确认；捕获 45 秒后串口已关闭，没有记录到所检查的 panic、断言、无效段、面板绘制错误或 underrun。

候选包含：单麦 NSNet3 降噪、锁屏/解锁完整水波纹、默认关闭的竖排阅读、六位数字解锁密码、正文上下滑动翻页、正文图片长按 1 秒查看。主机 57 项测试和主构建验证已通过。

这是安装与启动验证，不能替代密码输入、录音、真实触摸、灰阶及锁屏残影的实机验收。用户清单：`build/image-hold-20261112/DEVICE-ACCEPTANCE.md`；执行证据：`build/image-hold-20261112/device-install/result.json`。完整备份和串口原始日志仅保存在本机 `build/`，不发布或提交。
