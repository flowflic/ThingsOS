# ThingsOS Metalio 0.1.1（2026-10-31）

按用户更正后的版本要求，将项目及默认主/救援固件版本统一为 **0.1.1**。
`native/VERSION` 是主机 CMake 项目、主应用、救援固件和默认构建脚本的共同版本源。
设置页及启动日志读取固件自身的 ESP 应用描述符。

本版本包含此前的键盘命中/坐标校正、中文候选和拼音裁切、输入区白底维护，以及
今天/明天单次闹钟、周期起止日期和胶囊开关。详见
[键盘说明](KEYBOARD-TOUCH-AND-HINT-20261031.md)、
[闹钟说明](ALARM-DATE-RANGE-20261031.md)。

## 构建

```powershell
./native/build.ps1 -Rescue
./native/build.ps1
python ./native/package_metalio.py
```

输出位于 `build/native-rescue/`、`build/native-idf/` 和 `build/native-usb/`。
构建和打包不连接设备、不烧录、不发布，也不提交 Git。

## 验证与设备状态

主机项目配置、主应用及救援应用描述符均已确认是 **0.1.1**。
主应用、救援固件构建和打包通过，JPEG 解码器链接检查通过；
分区布局、各镜像内容和哈希、USB 包中的擦除态 NVS 与主应用启动选择数据均校验通过。

| 镜像 | 字节数 | SHA-256 |
| --- | --- | --- |
| `build/native-usb/ThingsOS-metalio-0.1.1-usb.bin` | 16,610,104 | `fe220a2a0a58a03d779482dd707f3095d2bbd3cef6e81591f24f09f653a5bc98` |
| `build/native-usb/ThingsOS-metalio-0.1.1-ota.bin` | 6,496,288 | `da3254aa8fb15d7a7ff90d06b8ec57dbd0cf54652c0b19d24af09c666fa628bd` |
| `build/native-rescue/ThingsOS_rescue.bin` | 1,350,096 | `45966d7aea8e350901afd9e7a0e64e91bfa5fac0b015ddf1abce0661b054a53b` |

清单：`build/native-usb/metalio-manifest-0.1.1.json`。
验证结果：`build/version-0.1.1-verification.json`。
构建日志：`build/version-0.1.1-main-20261031.log`、
`build/version-0.1.1-rescue-20261031.log`；打包日志：
`build/version-0.1.1-package-20261031.log`。

之前的完整主机功能回归为 42/42 通过，见 `build/alarm-date-host-20261031.log`。
本轮只更改版本及当前说明，历史固件、哈希和验收记录不重新标记为 0.1.1。

设备当前运行 **0.1.0 闹钟候选**，其完整回读及启动健康证据见
`build/alarm-date-20261031/usb-install/result.json`。
0.1.1 尚未安装；键盘实际效果、新闹钟功能与显示观感仍需用户验收。

## 官网发布（2026-10-31）

随后按用户“0.1.1 更新到官网”授权，已更新
[官网 Metalio 渠道](https://thingsos.scholarcn.com/#installation) 的版本、更新说明、
USB/OTA 镜像及 `metalio/release.json`、`metalio/manifest.json`。
发布保留开发测试版标识；Pico 渠道维持 0.3.9，镜像与清单保持一致。

Vercel 生产部署：`dpl_5xEfDBmJoSPvSCB7y3jtyyzpeWgu`，状态 READY。
部署后从正式域名下载两款设备的 USB/OTA 镜像、Metalio stub 与工具，
校验大小和 SHA-256 一致；Metalio 各分区的 SHA-256/MD5 及主/救援应用版本通过。
网站实际 Metalio 校验器接受新包，六组损坏/不兼容情形拒绝；未执行设备烧录。

发布包与记录位于 `build/publish-metalio-0.1.1/`：`prepared.json`、
`deploy.log`、`online-verification.json`；发布网页在 `vercel/site/`。
官网源码属于 `D:\crossmux\ThingsOS\native\site`，其页面与原有两套网站暂存目录
已同步到本次版本。此处 `native/package_site.py` 仍是旧 Pico 打包路径，
不可用它重新生成 Metalio 发布清单。后续发布应检查 Metalio 版本来源和清单，
避免用旧的硬编码 0.1.0 更新说明覆盖本次内容。
