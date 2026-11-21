# USB 烧录工具（本地网页）

给 Metalio E-Ink4-Plus（ESP32-S31）准备的一次性测试工具：网页点一下，把
`build/native-idf/ThingsOS_native.bin` 写进主应用分区，回读校验，复位后抓 60 秒启动日志，
直接给出"通过 / 未通过"结论。

- 只绑定 `127.0.0.1`，不对外网开放。
- 使用本机已安装的 esptool 5.5.0（`D:\crossmux\.tools\idf-tools\python_env\idf6.1_py3.12_env`），不新增依赖。
- 不写 16 MiB 全量镜像，不碰 bootloader、分区表、rescue、字体分区，不写 TPS65185 VCOM。
- 设备串口需要用户自己授权后才会被占用；工具自身不会自动开始烧录。

## 启动

```powershell
cd D:\ThingsOS_nologotech\native\usb-flash-tool
.\run.ps1
```

浏览器打开 `http://127.0.0.1:8848/`（`run.ps1` 默认会自动打开）。端口冲突时：

```powershell
.\run.ps1 -Port 8850
```

停止：在运行窗口按 Ctrl+C。

## 使用

1. 串口下拉选 `303a:1001 · ESP32-S31` 那一个（默认已选）。
2. 镜像默认选最新的 `build/native-idf/ThingsOS_native.bin`，也可以直接填别的应用镜像路径。
3. `同时写入启动选择（0xf000）` 默认勾选，保证设备启动的是主应用而不是 rescue。
4. 点"开始烧录并检查"，日志面板实时显示 esptool 输出，结束后给出结论。

## 检查项

写入阶段：

- 镜像必须是 ESP32-S31 应用镜像（magic、chip id 32），长度不超过 `0xAE0000`。
- 写入地址固定 `0x220000`，写后回读该区间 MD5 与本地文件比对。
- 启动选择数据与已验证布局逐字节一致时才写，写完再回读比对。

启动阶段（复位后抓日志，最多 60 秒）：

- `Loaded app from partition at offset 0x220000`
- `UI ready:`
- `Startup health confirmed: display, font, touch`（出现后再多抓 2 秒即停）
- 日志中不出现 `Guru Meditation`、`assert failed`、`invalid segment`、`Panel draw error`、`underrun`

## 证据

每次运行写入 `build/usb-flash-tool/<时间戳>/`：

- `run.log`：完整日志
- `result.json`：结论、检查项、镜像 SHA-256

## 限制

- 不做 16 MiB 全量备份；需要备份或由 rescue 恢复时走
  `native/docs/METALIO-USB-FLASH-PROCEDURE.md` 的 esptool 流程。
- 中断烧录可能留下不完整的主应用；此时 rescue 仍可用，重新烧一次即可。
- 只处理一个任务；同一时间只允许一个烧录。
