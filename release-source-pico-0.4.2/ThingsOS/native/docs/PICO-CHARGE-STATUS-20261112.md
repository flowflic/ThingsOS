# Read Pico 充电图标修复

## 定位

0.4.1 的 Pico 状态栏已有闪电绘制，但 `app.charging` 只在开机及每分钟时钟刷新时更新。
用户插入或拔出充电器后，PMU 状态即使已经变化，图标仍可能等待近一分钟。
板级判断还仅接受 `PMU_CHARGE_CHARGING`，排除了接电充满的 `PMU_CHARGE_FULL_INFERRED`。
此前设备启动日志记录过 `battery=100% charging=0`；该日志未记录 PMU 原始枚举，
不能仅凭此认定当时一定处于充满状态。

官方 BSP 的 PMU 协议将充电中、接电已满、未充电区分为独立枚举；
充电引脚会脉冲变化，不能直接用引脚瞬时电平或 USB 枚举替代 PMU 的判断。
本次未修改官方 BSP、PMU 固件、充电电路参数或 VCOM。

## 修复

- 主任务每秒读取一次有效 PMU 电源状态，与每分钟时钟刷新分开。
- 充电中与接电已满均保留接电闪电标记；拔出后按 PMU 状态移除。
- 读总线或 STATUS 校验失败时保留上一份有效状态；无效 SOC 不覆盖有效电量。
- 状态变化单独触发状态栏更新；整页已绘制最新状态时不重复刷新。
- 按用户纠正，使用 Metalio E-Ink4-Plus 当前批准的 49×23 电池和 14×17 实心闪电绘制。
  只迁移 `status_charge_ink`、`status_battery_draw` 两个视觉函数；抗锯齿覆盖率、随填充反色、
  相对位置与原实现一致。没有迁移 Metalio 的 GPIO 检测或刷新周期。

视觉来源：`D:/ThingsOS_nologotech/native/firmware/main/status_power.inc`，Apache-2.0。
Pico 实现：`native/firmware/main/status_power.inc`。

## 验证记录

证据保存在 `build/pico-charge-status-20261112/`，最终状态及镜像哈希以 `result.json` 为准。

最终主应用、救援镜像构建及集成检查通过，43/43 项主机回归通过。

- `board-test.log`：从当前板级源函数提取测试，使用官方 PMU 类型，覆盖充电/充满/未充电/
  未知/故障、状态 CRC 无效、总线错误、设备不存在、0%/100% 与无效 SOC 保留。
- `device_ui_test.c`：执行实际采样和绘制控制器，覆盖一秒检查、无状态变化不触发刷新、
  读取失败保留、插拔切换、0/1/40/50/99/100% 的图标可见性、拔出后像素恢复及横竖屏一致。
- `reference-icon-verification.json`：两个视觉函数与 E-Ink4-Plus 参考代码逐字比较一致。
- `charging-icons.png`：实际主机 UI 控制器生成的状态栏截图裁剪，非设备照片。
- 回归发现现有预排版取消测试的 `Sleep(10)` 在 Windows 调度下不能保证已完成一行。
  主机测试支架改为等待实际进入下一行的事件，保留取消、部分页不发布和恢复结果的断言；
  `THINGSOS_HOST_UI` 之外的设备预排版逻辑不变。

版本按用户政策保持 0.4.1。2026-11-13 按用户授权已同版本更新官网，
两个生产域名的 USB/OTA 下载与哈希验证通过，见 `PICO-0.4.1-RELEASE-20261112.md`。
本次未烧录、未提交；充电插拔实际响应时间及墨水屏显示仍待设备验证。
