# 录音提示、状态栏与音量浮层

2026-10-28，按本会话确认的 [V3 原型](../../build/prototypes/recording-status-20261028/index-v3.html) 实现。
仅修改 Metalio 应用层，版本标识不变；未烧录、未发布、未提交。

后续状态：用户另行指定 beta 版本并授权设备安装，`0.0.1-v1-beta` 已完成烧录、逐字节回读与启动健康核对。详见 [设备记录](BETA-V1-DEVICE-VALIDATION.md)；原始构建阶段的版本与未烧录记录保留在下文。

## 最终界面

- AI 键开始录音后，服务真正进入录音状态才显示「开始录音」。白底黑框卡片为 456 × 240，按当前画布居中，3 秒后收起；准备期间显示等待状态。
- 结束录音先显示「正在保存录音」。只有音频工作线程完成 WAV 更新、同步和关闭并报告 IDLE，才显示「录音已保存」，再保留 3 秒。提交失败、初始化失败和保存失败不会被标为成功；错误卡片保留到轻触关闭或下一次录音操作。
- 状态栏麦克风为 30 × 36：圆角胶囊位于 `(8, 3)`，相对初版柔和图标下移 3 px，弧形支架宽 3.5 px。使用 4 × 4 覆盖采样处理边缘，后台录音期间静态显示；不增加闪烁或计时动画。
- 普通页面移除顶部系统名称，时间采用阅读页已有的 `x = 28` 左对齐布局；阅读菜单和图片视图原有的返回/时间布局保留。
- 音量浮层参考用户提供的 Apple 音量胶囊：84 × 312，距右侧 28 px、垂直居中。按实际百分比从底部连续填充，扬声器在胶囊内部下方，按填充区域反色；0% 显示静音斜线。最后一次调节后 1.6 秒收起。

阅读页实体音量键原有的翻页映射、控制中心内的音量滑块保留。浮层绘制支持 684 × 1216 和 1216 × 684；录音提示和音量浮层出现、消失时使用已有全局 UI / 阅读背景呈现路径，避免音频进度或控制中心局部掩码漏掉中央/右侧区域。

## 实现位置

- `native/firmware/main/main.c`：状态生命周期、超时、状态栏、全局呈现和错误卡片关闭。
- `native/firmware/main/apps_controls.inc`：AI 键请求及音频服务确认、音量更新。
- `native/firmware/main/alignment.inc`：关闭 AI 键录音功能时复用同一保存流程。
- `native/firmware/main/feedback_icons.inc`：独立绘制的平滑麦克风、扬声器及几何辅助。
- `native/firmware/main/audio_feedback.inc`：中央卡片和连续胶囊音量浮层。

本轮没有改动音频工作线程、板级驱动、波形、VCOM、充电策略或参考 checkout。Read Pico 源树未改动。新增应用绘制代码保留 Apache-2.0 标识，不引入 Apple 图片或外部运行时。

## 验证与证据

证据目录为 `build/recording-status-20261028/`；修改前已有未提交工作，相关文件备份和哈希保存在该目录的 `baseline/`、`baseline-hashes.json`，没有覆盖或暂存其他修改。本轮镜像也包含工作区原有改动。

主机 `device_ui_controller` 用实际固件绘制和事件代码验证异步启动/停止、成功后才提示、保存失败、重复按键、启动被取消、3 秒自动消失、1.6 秒音量超时重置、连续填充（包含 54%/55% 差异和 0%/1% 边界）、静音、上下界、横屏位置及首页/阅读时间对齐。

主机帧缓冲输出包括：

- [录音开始](../../build/recording-status-20261028/recording-feedback-start.png)
- [保存完成](../../build/recording-status-20261028/recording-feedback-saved.png)
- [保存失败](../../build/recording-status-20261028/recording-feedback-error.png)
- [连续音量胶囊](../../build/recording-status-20261028/volume-capsule-portrait.png)
- [横屏音量](../../build/recording-status-20261028/volume-capsule-landscape.png)

这些是主机渲染证据。设备上的图标观感、灰阶/残影、提示消失后的底图恢复，以及实际录音文件保存仍待实机验收。

构建与回归命令、最终结果及镜像指纹见证据目录内的 `validation.json`、`host-build.log`、`host-tests.log` 和 `firmware-build.log`。

最终结果：MSVC 主机回归 **32/32 通过**；ESP-IDF v6.1 的 ESP32-S31 主应用构建通过。应用镜像 6,478,880 bytes，位于 11,403,264-byte 的 `ota_0` 容量内；SHA-256 为 `97ea5ceeebfa9ec88d2b61a9c246c6405317d2d17105ed9d43c0efac76c74621`。本工作区当前构建描述符版本为 `0.0.1`，与修改前一致，没有执行版本升级。
