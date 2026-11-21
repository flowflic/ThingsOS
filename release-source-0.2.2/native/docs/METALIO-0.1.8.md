# Metalio 0.1.8 发布

2026-11-14 用户确认 `native/release-notes.json` 中十条更新说明，并授权更新项目与主／救援固件版本、提交 Git、上传官网。上一正式版为 0.1.7；官网同时提供 Read Pico 0.4.1，本次保留其元数据、固件及历史。

完整更新说明以 `../release-notes.json` 为准。包括阅读交互、当前目录定位、大 TXT/GBK、解锁密码、录音降噪、离线时长、微读同步、加载显示恢复、系统指标/欠载统计及网页上传进度位置。

验证：当前完整主机回归 61/61 通过。主／救援分别使用 `native/build.ps1` 和 `native/build.ps1 -Rescue` 构建，再用 `native/package_metalio.py` 生成 USB/OTA 包。发布证据目录 `build/publish-metalio-0.1.8/`，包含基线、构建、回归、打包与线上校验。

此前 0.1.7 同功能开发候选已完成救援优先 USB 安装、完整回读及启动健康检查。0.1.8 发布任务不访问设备串口，发布不代表设备已升级。真实账号时长与进度入账、条纹复现和光学效果仍待对应实机验证，不把保护措施表述为条纹已根治。

## 对应源码与构建

官网 Metalio 面板提供本版对应源码 ZIP 和独立第三方声明。原 ThingsOS 源文件许可保持；包含的 AGPL/GPL 改编模块及组合分发条件见 `licenses/READTIME-SOURCES.md`，不能将组合固件整体描述为仅采用 Apache-2.0。

源码包从发布 Git 提交导出，附带实际使用的官方参考组件（epdiy、e0470_epaper_waveform、sc7a20h，位于原引用路径）及其许可；不包含 `.git`、用户 NVS、账户令牌、设备备份或运行日志。ESP-IDF 为 v6.1，构建用固定修订 `fff9895c82d744c7237be8847347bdd1b07c6643`（工作树无本地修改），获取该修订及工具链后设置 `IDF_PATH`、`IDF_TOOLS_PATH`。在 Windows PowerShell 中从源码根运行上述构建命令。仓库内通用依赖由 `native/prepare_dependencies.py` 验证；本地 CMake 适配保留在源码中，未修改参考目录。

同目录提供 `SOURCE-RELEASE.json`，记录发布 Git 提交、参考组件和源码包哈希。硬件分区与安全约束见 `native/docs/METALIO-PORT.md`、`AGENTS.md`；修改后安装仍遵守救援优先，不应覆盖运行中的主应用分区。
