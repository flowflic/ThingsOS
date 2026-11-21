# Metalio 单开关离线阅读时长候选 USB 安装

2026-11-13，用户明确要求烧录；版本保持 0.1.7。

安装包含离线阅读时长单开关交互、Native 授权二维码大响应接收修复，以及目录当前章节浅灰背景和打开时居中定位。既有 PIN 输入优先和快刷修复保留。

- 设备：Metalio ESP32-S31，USB 序列号 1C:29:04:D0:4A:B0，本次端口 COM5。
- 主镜像：6851488 字节，SHA-256 02645c75cf113c08fa6b1a0fca93bc0b8cd687080cd4edb2fbbf84eb842b2838。
- ELF SHA-256：ae59c7065c7f2ff9d8464751aae367a23bf8a51b7638b2959070a3ec0d1b3477。
- 完整 16 MiB 备份 SHA-256：d6fb77fb19cd98e3c5bc3741143da166e7b96b917fdf196faa8c577ff7521ed8。

已完成原主应用启动健康确认、完整备份、救援实际启动及 45 秒观察、救援后 NVS 快照、仅写主槽 0x220000、完整 16 MiB 回读、镜像和保护区验证、最后写启动选择器并回读、新应用 45 秒启动观察。

引导、分区表、PHY、保留区、救援和字体均与备份一致；主固件写入未改变 NVS。NVS 与首次原始备份一致：True。主应用从预期主槽启动，版本与 ELF 标识符合候选，初始页面为 library，display/font/touch 启动健康通过，未检测到指定崩溃或面板错误。串口已关闭。

首次完整备份后，因启动日志缺少 app_init 版本/ELF 行，安装保护在任何 Flash 写入前停止。原镜像 SHA-256 已与上次安装一致。保留该次备份和日志；独立重试目录增加有界重新采集：仅完整观察 45 秒且健康的启动，才允许为缺失元数据重采一次，全部镜像、版本、ELF、设备和保护区断言保留。

最终结构化证据：build/readtime-toggle-20261113/usb-install-20261113-retry1/device-install/result.json。首次停机证据：build/readtime-toggle-20261113/usb-install-20261113/device-install/。源文件、候选哈希和主机回归范围见 build/readtime-toggle-20261113/validation.json。

本次完成安装与启动健康检查；实际扫码授权、离线入账、目录视觉和长期稳定性仍需用户操作验收，未发布或提交。
