# ThingsOS

当前独立工程版本为 **0.4.2**，主应用与救援位于 [native/](native/README.md)。官网为 https://thingsos.scholarcn.com/，仅提供当前版本固件及历史更新日志。以下旧实现与早期状态保留为历史记录。

> **2026-10-11 开发路线更新：** 用户要求不照搬 kiikoread 底层。
> 当前 `main/`、`components/` 和已发布 0.1.1 属于旧实现，包含 read-pico-reader
> 派生代码；不能称为独立重写版。新实现位于 [native/](native/README.md)，
> 不通过旧应用源码搭建系统。下文的功能和烧录说明描述历史 0.1.1，
> 并不表示新实现已完成或可烧录。已确认原型及后续产品要求继续适用。

新工程入口：[native/README.md](native/README.md)，架构与模块状态见
[native/ARCHITECTURE.md](native/ARCHITECTURE.md)。交互基准为 v3；新工程
构建使用 `native/build.ps1`。下文 0.1.1 命令仅用于历史工程。

2026-10-11 独立工程已接入屏幕/完整字体、EPUB 图文、WiFi/HTTP/OTA 与单独救援
应用，并接入设备键盘、SD 文件浏览、书签与阅读统计持久化。开发产物与测试证据
见 native；尚未进行设备验收。用户授权的 0.2.0 独立工程开发测试包已更新到
https://thingsos.netlify.app，旧版 0.1.1 保留在 history/0.1.1；旧版需完整 USB 迁移。

Read Pico RDP-G01-W / ESP32-S3 自定义阅读固件，0.1.1 开发版。

按设备官方开发指南使用 ESP-IDF v6.1。官方 BSP 提供显示、触摸、SD 与电源接口；阅读实现基于 read-pico-reader 的 EPUB/TXT 引擎，CrossMux 用作架构与产品参考。确切来源版本见 [upstream-sources.json](upstream-sources.json)，保留原有许可证及源码版权声明。

## 首版功能

- EPUB、TXT 合并显示在同一个书库，不按格式分栏；开机进入书库。
- 本地阅读、章节目录跳转、SD 文件目录浏览、书籍目录设置。
- 完整内置 `方正屏显雅宋.TTF`，作为系统界面和阅读默认字体；无需 SD 卡。其余字体从 SD 根目录 `/fonts` 加载，不内置。
- 增删书签、阅读进度与跳转、阅读时间统计及最近阅读。
- WiFi 连接 / 热点传书，浏览器管理书籍及字体。
- HTTPS 网页 USB 完整烧录；局域网浏览器 OTA 应用升级。

SD 推荐目录：`/books` 放 EPUB/TXT，`/fonts` 放 TTF。设备内挂载路径分别为 `/sdcard/books` 与 `/sdcard/fonts`。所有用户书籍只写入 SD 卡。内部仅提供一个只读 `Example.txt`，内容严格为 `Example Test`（12 字节，无换行），直接嵌入固件，不建立可写书籍文件系统；上传、编辑、重命名和删除均被拒绝。无 SD 时仍可阅读这个示例；常规浏览器 OTA 暂存需要已挂载 SD 卡。

默认字体原文件位于 `fonts/方正屏显雅宋.TTF`，大小 2,978,568 字节，共 8,103 个字形。构建脚本逐字节复制为 `build/system-font.bin`，不裁剪字符、不修改字体。运行时映射 `sysfont` 只读分区并验证 SHA-256；该分区容量 5 MiB，双 OTA 槽各 4 MiB，独立救援分区为 2.875 MiB。最小应急字体仅用于恢复或默认字体校验失败时，其他用户提供字体不进入固件。更换内置字体资源需要完整 USB 安装，普通应用 OTA 不覆盖字体分区。

## 编译与打包

在 Windows PowerShell 中安装 ESP-IDF v6.1 与 ESP32-S3 工具。工作区预置环境位于 `../.tools/esp-idf` 和 `../.tools/idf-tools`；其他机器可设置 `IDF_PATH`、`IDF_TOOLS_PATH` 后使用同一脚本。

```powershell
.\tools\build_thingsos.ps1
# 新版本应修改默认 PROJECT_VER，或显式指定：
$env:THINGSOS_PROJECT_VER = '0.1.1'
.\tools\build_thingsos.ps1 -Package
```

打包依赖本地 `esp-web-tools@10.4.0`：`npm install --prefix ../.tools/web-deploy esp-web-tools@10.4.0`。

网站输出到 `build/site`。打包脚本读取当前构建的 `flasher_args.json`，使用 esptool 合并并逐字节核对实际分区镜像，生成固件 SHA-256、刷机清单、对应源码与许可说明。`*-usb.bin` 是完整 USB 包，`*-ota.bin` 是应用升级包。不要混用。

USB 安装入口：[thingsos.netlify.app](https://thingsos.netlify.app)。完整 USB 安装会替换现有固件，可能清除内部设置和数据；先备份。

当前新增的字体分区与独立救援布局必须通过 ThingsOS 打包脚本完整安装；不要使用 IDF 自动建议的原始 `flash` 命令。打包脚本会拒绝缺少独立救援镜像的构建，不会把主阅读程序写入容量更小的救援分区。0.1.1 的布局与旧版 0.1.0 不兼容，首次升级必须使用完整 USB 包。

## OTA

设备进入“传书”，连接 WiFi 或热点，浏览器打开设备显示的地址，再进入“固件升级”。上传不同版本的 ThingsOS 应用 `.bin`，初步检查后确认安装。芯片、项目、版本与大小均需匹配；写入备用分区并通过 ESP-IDF 完整校验后才设置下次启动分区。新版本正常启动后确认，否则依赖 bootloader 回退。成功后自动重启。

OTA HTTP 接口：`GET /api/firmware`，`PUT /api/firmware` 上传，`POST /api/firmware/install` 确认。修改请求需同源并携带本次服务会话的 `X-ThingsOS-Token`。该机制阻止跨站请求，局域网内持有设备地址的客户端仍可访问管理页；请在可信网络使用。

TF 卡升级文件名为 SD 根目录的 `ThingsOS-update.bin`。首版关闭继承自参考项目的云端自动升级源。

## 验证边界

已验证：ESP-IDF 全量编译、OTA 主机失败路径测试、阅读进度主机测试、传书页面交互、刷机页 SHA-256/清单/确认门槛、OTA 网页状态流程。测试源码位于 `tools/thingsos_ota_host_test.c`、`tools/thingsos_web_host_test.js` 和原阅读引擎主机测试。

尚未实机验收：屏幕刷新、触摸命中、真实书籍分页与字体切换、WiFi 传输、USB 安装、掉电与启动回退、续航与阅读计时。当前是可构建开发固件，不能以编译成功替代这些验收。原型已确认；首版复用原生阅读组件，所有原型页面的视觉一致性仍需实机调整。

出厂 VCOM 仍由 PMU 读取，不改写。Flash/PSRAM 时序沿用 Read Pico 目标配置，不将编译结果视作硬件兼容证明。

官方开发指南：https://dot.mindreset.tech/docs/read_0/firmware

## 网站部署

站点名称 `thingsos`；Netlify 项目 ID `d7434916-1a63-4a48-a86e-dba9f905346c`。先编译并打包，再从项目目录执行：

```powershell
netlify deploy --prod --site d7434916-1a63-4a48-a86e-dba9f905346c --dir build/site --no-build
```

`.netlify/` 已加入忽略列表，认证凭据不进入源码和网站目录。继承的上游发布脚本面向原项目；ThingsOS 使用 `build_thingsos.ps1` 和 `package_thingsos.py`。

Vercel 镜像使用同一份 `build/native-site-current` 静态包。发布脚本会把它复制到独立的 Vercel staging 目录，应用 `native/site/vercel-project.json` 中的响应头配置，并且不会上传固件源码目录。首次运行脚本后，按提示将 staging 目录链接到 Vercel 上的 `thingsos` 项目；以后打包完成后再次运行：

```powershell
.\native\deploy_site_vercel.ps1
```

`ThingsOS` 工作目录不是 Git 仓库，所以该命令从本地打包目录发布；每次打包后需再次运行以同步新包。它不会触发 Netlify 发布。Vercel 项目链接和下载的凭据保存在 `build/native-site-vercel/` 中，不进入静态发布目录。

## 独立救援

主应用启动后须触摸与显示健康持续 30 秒才确认 OTA，启动 120 秒未通过检查则进入救援。主循环 60 秒无响应会复位；连续三次异常热启动由 bootloader 转入 factory 救援。预期的锁屏睡眠与 OTA 写入会暂停主循环检测。救援应用独立于 SD、屏幕、字体、PSRAM 和用户 NVS；只初始化供电 PMU、WiFi 热点和 HTTP OTA 服务，不改写 VCOM。连接 `ThingsOS-Rescue-xxxx`，密码 `thingsos-recover`，打开 `http://192.168.4.1`，上传本网站 OTA 包并确认启动。救援写入备用应用槽，SDK 完整校验后才切换；不写字体或自身分区。还提供进入 ROM USB 下载模式的按钮。所有恢复与断电行为仍需实机验收。


### USB 直连模式

设备设置中的「USB 直连模式」提供开启和关闭按钮。开启前连接 USB 数据线；
进入后电脑会将整张 SD 卡识别为可移动磁盘。书籍放入 `books/`，字体放入
`fonts/`，壁纸图片放入 `wallpapers/`。设备不会格式化 SD 卡。

直连期间暂停阅读及无线传书，SD 卡由电脑独占。完成复制后建议先在电脑安全弹出
磁盘，再在设备点击「关闭 USB 直连」。设备上的关闭按钮会直接生效，不依赖
电脑是否发送弹出状态；它会等待当前扇区读写完成后主动断开 USB。设备会重新启动并回到设置，重新加载
SD 卡及书库。直连不是持久开机选项，正常启动不会自动向电脑开放 SD 卡。
