# ThingsOS Metalio 与 read-pico-reader 刷新处理对比

## 2026-11-02 最新同机型核对

最新官方 2.0.72（`e703fd35269324cd163f1deae0a2fb5f7cf5f710`）的刷新已拆到
`main/display/epd_refresh.cc`。`Draw()` 默认 `keep_power=false`，325–327 行在输出后断轨；
393 行仅为翻页动画中间帧传 true，最终输出 529 行传 false，错误也会断轨。
不是阅读结束后另保 3 秒。见 [官方源码](https://github.com/CloudZao/Metalio-E-INK4-Plus/blob/e703fd35269324cd163f1deae0a2fb5f7cf5f710/main/display/epd_refresh.cc#L325-L327)。

kiikoread S31 最新 `83942eb45d13064a47aca7ff04c2faf8fd51f98a`，
`boards/metalio47/src/main/display.c:96–107` 的 `rails_keepalive()` 实际先释放驱动再立即
`epd_poweroff()`，释放失败 50 ms 后重试；dpReader31 最新 1.2.6-vn
`a41c9fe96993290052782dc767162f253a4ab26f` 的同路径 99–110 行采用相同策略。
这两份是有关联的同机型代码，不能当作完全独立的三份实测证据。
[kiikoread](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/83942eb45d13064a47aca7ff04c2faf8fd51f98a/boards/metalio47/src/main/display.c#L93-L107)、
[dpReader31](https://github.com/dephia31/dpReader31/blob/a41c9fe96993290052782dc767162f253a4ab26f/boards/metalio47/src/main/display.c#L96-L110)。

本项目后来为刷新性能加入了 3 秒保电，与下文历史中已通过静置的立即断轨版本不同。
用户再次反馈麻点后，新候选将阅读页恢复立即断轨，普通界面仍保留 3 秒；静置光学结果待实机。
插图故障与此次候选见 [2026-11-02 定位记录](EPUB-INLINE-IMAGES-20261102.md)。

## 以下为 2026-10-25 历史对比

核对日期：2026-10-25。参考主仓 `wegooo-cell/read-pico-reader@108402737d1fc0f28060b13c4bcc3cbe884b93d9`（Pico rc100 / Metalio rc104 发布入口）；从其 Release 定位到同机型精确源码 `wegooo-cell/Metalio-S31-kiikoread@675af7351c5052506e6eefea621d7c15e2569530`。以下以实际编译选中的板级源码为依据，不把 README 中的旧描述或未启用实验当成当前设备行为。

## 面板断电与触控

ThingsOS 的 FT6336U 触控使用独立 I2C 0x38、GPIO5 中断及 TCA P1.2 复位。候选断电只清 VCOM_CTRL/PWRUP/WAKEUP，保留 P1.2、前光和其他扩展器输出，主任务与触控采样不休眠。因此关闭墨水屏高压并不等于关闭触控；再次刷新需要重新上电，增加的耗时必须实机测量。

## 三个状态必须分开

| 项目 | 设备上一版（2b0cc5d8…） | 本次已安装候选（54461608…） | 参考 Metalio rc104 |
| --- | --- | --- | --- |
| 普通正文 | GC16 派生36相，白底完整黑白复位 | GC16 派生36相，恢复 Pico 单白脉冲 | 默认完整 GL48 派生，保持相同非白灰阶；实验路径单独开关 |
| 未变白底 | 11 黑/17 白，导致每页黑闪；用户确认静置仍有散点 | 不主动压黑，只保留一个白向脉冲 | 在已有相位中增强尾部向白驱动，不把未变白底先压黑 |
| 面板电源 | 更新后保留8秒 | 更新结束后立即关闭 | `rails_keepalive()` 名字保留，但 Metalio 实际立即 `epd_poweroff()` |
| VCOM使能 | 绑定每个 ep_mode 边沿 | 整次更新内保持稳定，结束后关闭 | TPS 上电到下电之间保持，XOE/MODE 独立门控 |
| 输出关闭检查 | 没有独立的真实输出读回 | 读取 TCA 输出与开启输入缓冲的 XOE/MODE；异常报错 | 显式 release_drive；失败50ms后重试；不因此重画页面 |
| 后台扫描 | 时间/电量/分页/笔记可能引起局部扫描 | 保留逻辑并记录每次更新后的断电结果 | 正文状态栏与正文分开，局部反馈不随意消耗整页清理计数 |
| 欠载恢复 | 20MHz失败降10MHz，清屏后重建 | 保留 | 失败不伪造back，回到全表/白基线并降安全时钟 |
| 电压参数 | 本机1200mV，来自原 Metalio vendor port；没有新标定 | 不变，无EEPROM写入 | 对方有自身默认/本地NVS标定；不能视作本机标定值移植 |

Pico 与 Metalio 的参考实现也不同：Pico 保轨8秒但在每次输出后立即释放像素驱动；Metalio 直接断轨。仅看到 `rails_keepalive` 函数名或共用 API 不足以判定硬件实际行为。

## 最值得参考的顺序

1. **先确认输出与供电确实退出。** 当前候选已恢复低闪，并加上整次更新的稳定使能、立即断轨及数字控制输出读回。它与参考 Metalio 的总体生命周期一致；暂不同时增加白脉冲，先区分电源因素。
2. **若输出退出正常仍静置漏灰，再独立比较仅向白维护。** 参考同机型波形源码明确记录“单白脉冲在强光下几秒漏灰”，采用已有尾相里的白向补推，不做整页黑白循环。这个现象记录与本次反馈相似，但还不是本机的根因证明。实现应独立编写，并保留尾部稳定、字缘灰度和温度 A/B 验证。
3. **再讨论 GL48 与 GC36 的取舍。** 参考默认正文是GL48，发布记录中的GL37/跨页39相主要是书架/切主页，不能直接套给正文；22相属于显式实验。不要把“相数更少”当成“更稳或无残影”。
4. **保留真实 front/back、故障重建、局部反馈与正文清理分账。** 不添加无依据的延迟整页重刷，也不靠每页黑闪掩盖静置问题。

这次仅阅读、分析参考源码；没有把其修改过的驱动复制进 ThingsOS。ThingsOS 的现有独立实现和 Metalio 原参考驱动边界保持。

## 可点击源码依据

- [Metalio rc104发布与精确源码身份](https://github.com/wegooo-cell/read-pico-reader/releases/tag/metalio-s31-v0.3.3-rc104)
- [参考Metalio：输出后释放并立即断轨，50ms重试](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/675af7351c5052506e6eefea621d7c15e2569530/boards/metalio47/src/main/display.c#L91-L105)
- [参考Metalio：XOE/MODE和触控/高压的板级边界](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/675af7351c5052506e6eefea621d7c15e2569530/boards/metalio47/src/components/read_pico/read_pico_board.c#L39-L61)
- [参考Metalio：单白脉冲静置漏灰记录及尾部擦白](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/675af7351c5052506e6eefea621d7c15e2569530/boards/metalio47/src/components/e0470_epaper_waveform/e0470_epaper_waveform.c#L311-L336)
- [参考Metalio：默认GL48正文与独立实验分流](https://github.com/wegooo-cell/Metalio-S31-kiikoread/blob/675af7351c5052506e6eefea621d7c15e2569530/boards/metalio47/src/main/display.c#L806-L850)
- [参考Pico：立即释放驱动、保轨8秒及重试](https://github.com/wegooo-cell/read-pico-reader/blob/108402737d1fc0f28060b13c4bcc3cbe884b93d9/main/display.c#L64-L97)
- [参考Pico：零变化也必须释放驱动](https://github.com/wegooo-cell/read-pico-reader/blob/108402737d1fc0f28060b13c4bcc3cbe884b93d9/components/read_pico/read_pico_board.c#L410-L421)

本机证据：`build/reader-speckle-fix/static-current-live.log`、`build/reader-static-fix/host.log`（27/27）、主/救援构建日志。候选的设备安装结果独立保存为 `build/reader-static-fix/flash-validation.json`，光学验收仍待同页静置观察。

## 安装与观察进度

本次候选已完成备份、只写主应用、完整回读及启动健康确认；设置、字体、原救援均保持一致。27项主机回归通过。用户已打开原书参与同页静置观察；170秒日志中两次后台更新后的数字输出关闭读回均正常，没有扫描错误。黑点是否消失仍待用户光学反馈，因此不将该候选称为已完成面板验收。

## 实机结果更新

用户完成上述同页静置后确认“没有黑点，白底稳定”。本次保留供电生命周期修正及低闪文字路径，不追加参考分支的白脉冲增强，不变更相数或电压。结果仅覆盖本次约3分钟同页观察，不能推断长期全部场景通过。
