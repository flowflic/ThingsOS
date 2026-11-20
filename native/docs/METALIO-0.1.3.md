# ThingsOS Metalio 0.1.3（2026-11-01）

把项目及默认主/救援固件版本统一升级到 **0.1.3**，并让正式固件只保留必要的运行日志。
`native/VERSION` 是共同版本源；0.1.2 的发布包、安装记录与验收边界保留不动。

## 相比正式版 0.1.2

| 项目 | 更新 |
| --- | --- |
| 校时 | SNTP 使用 203.107.6.88、ntp.aliyun.com、cn.pool.ntp.org 三个服务器；系统时间未校准时用 HTTP Date 兜底，来源与时间写入 NVS `thingsos-net`，下次启动打印上次来源 |
| 亮度 | 控制中心与前光灯页面按 1% 步进；音量仍 5%，色温仍 10% |
| 刷新 | 成功刷新后保持面板电源轨最长 3 秒，连续帧省去约 90 ms 上电时序；空闲计时器断电并做空闲校验，休眠立即断电 |
| 微读 | 章节上下文预取任务的栈移入 PSRAM，空间不足时回退内部 RAM，腾出约 24 KB 内部 SRAM |
| EPUB | mimetype 允许尾部 CR/LF/空格；清单与书脊上限 512 提高到 1024，可打开 500 个以上章节的合集 |
| 书封 | 首次下载失败后间隔 30 秒重试，每本最多 3 次 |
| 更新检查 | 改用 `metalio/release.json`，板型 `Metalio-E-Ink4-Plus`、布局 `thingsos-metalio-single-v1`；连接失败后静默重试一次（间隔 10 秒），两次都失败才提示 |
| 日志 | 移除仅供诊断的 ZIP/EPUB 失败日志与刷新相位日志，运行行为不变 |

## 构建与验证

- 主固件与救援固件均由 `native/build.ps1` 以 `0.1.3` 构建；`native/verify_build.py` 校验应用程序描述符与 `native/VERSION` 一致。
- EPUB 上限修复已在设备上验证：6.2 MiB、572 章的《大秦帝国（精排全集）》可打开并正常翻页，证据见 `build/epub-limit-20261101/tap.log`。
- 电源轨复用实测：连续帧 `power=58µs`，间隔超过 3 秒后恢复为约 90 ms，证据见 `build/prefetch-psram-20261101/device-install/main-boot.log`。
- 微读预取栈迁移后启动健康、NVS 未变；打开书籍时日志打印 `Prefetch stacks: PSRAM`。
- 更新检查的清单与静默重试已安装验证。本网络首次 TCP 连接偶发被中断（`errno=113`），重试即可连上。

## 发布

- 2026-11-01 发布到 `https://thingsos.scholarcn.com/metalio/`：Vercel 生产部署 `dpl_3WxUxMwoe71qFKh4xYYbCjtx49PW`，生产别名 `https://thingsos.vercel.app`。
- 线上 `metalio/release.json` 为 0.1.3 / 上一版 0.1.2；USB `ThingsOS-metalio-0.1.3-usb.bin` 16,610,104 B、SHA-256 `98acc131d5a1d1ab97a0a62917d7ba702ee493fa04f2b333378436fbbfef0380`；OTA `ThingsOS-metalio-0.1.3-ota.bin` 6,512,144 B、SHA-256 `5a2180ea92436eab5ee73ee4d90698d374a57b2e6b0ada17b0a37420fc192fa8`。
- 整包 7 个分区（bootloader、partitions、nvs、otadata、rescue、app、font）逐段核对 SHA-256 与 MD5；0.1.2 的两个固件文件已下线并返回 404；页面 6 处版本标记为 0.1.3。
- Read Pico 0.3.9 的 86 个文件逐字节未变；证据见 `build/publish-metalio-0.1.3/online-verification.json`。

## 边界

- 0.1.3 已发布到官网，但尚未安装到设备；实机仍运行 0.1.2 发布后修正版（`755e34d8`）。
- 移除诊断日志后，EPUB 与压缩包失败不再输出原因；需要定位时再临时加回。
- 更新检查依赖设备到官网的 TCP 连通性，现场网络偶发中断时页面会多停约 10 秒再出结果。
