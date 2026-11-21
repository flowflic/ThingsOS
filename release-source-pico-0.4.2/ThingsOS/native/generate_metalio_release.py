#!/usr/bin/env python3
"""Generate the Metalio release metadata for the packaged USB image.

Reads build/native-usb/metalio-manifest.json and the USB image, then writes
metalio/release.json and metalio/manifest.json into the staged site. It never
modifies the packaged firmware, the official Pico release, or any device.

Part sizes come from the packaged manifest except the partition table (the
packaged record is 3072 B but the flash region is 4096 B) and NVS (erased on a
complete install, therefore absent from the package manifest).
"""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
USB_DIR = ROOT / "build" / "native-usb"

PART_ORDER = [
    ("bootloader", 0x2000, None),
    ("partitions", 0x8000, 4096),
    ("nvs", 0x9000, 0x6000),
    ("otadata", 0xF000, None),
    ("rescue", 0x20000, None),
    ("app", 0x220000, None),
    ("font", 0xD00000, None),
]

CHANGES = [
    "项目与默认主/救援固件版本统一为 0.1.0，由版本文件单一提供。",
    "首次支持无名科技 E-INK4 PLUS（ESP32-S31）的 USB 完整安装与 OTA 升级。",
    "阅读、微信读书缓存与离线阅读、热门划线、想法与阅读时长同步。",
    "新增快捷录音：顶部 AI 键开始或结束，不打断阅读，保存到 /sdcard/audio。",
    "新增录音上传 ima 知识库，手机扫码完成配对与配置。",
    "新增音乐播放（MP3 / WAV），支持蓝牙耳机输出与掉线暂停。",
    "新增闹钟：最多 4 个提醒，可按天重复并选用音乐目录中的铃声。",
    "新增前光亮度与冷暖调节，传输页可分别接收书籍与音乐。",
    "当前为开发测试版，音频、蓝牙与面板仍待实机验收。",
]

LIMITATIONS = [
    "尚未在任意 E-INK4 PLUS 实机完成完整的网页安装与功能验收。",
    "首次网页安装需要设备进入 BOOT 刷机模式（设置 → BOOT 刷机，或按住 BOOT 上电）。",
    "安装会写入引导、分区表、救援系统、主系统和系统字体，并清空 NVS。",
    "音频输出、蓝牙耳机、闹钟响铃与面板光学表现需要实机确认。",
]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", required=True, help="staged site directory")
    args = parser.parse_args()
    site = Path(args.site).resolve()

    package_manifest = json.loads((USB_DIR / "metalio-manifest.json").read_text(encoding="utf-8"))
    version = package_manifest["version"]
    usb_name = f"ThingsOS-metalio-{version}-usb.bin"
    ota_name = f"ThingsOS-metalio-{version}-ota.bin"
    image = (USB_DIR / usb_name).read_bytes()
    ota = (USB_DIR / ota_name).read_bytes()

    if len(image) != package_manifest["usb_image"]["bytes"]:
        raise SystemExit("USB image size does not match package manifest")
    if sha256(image) != package_manifest["usb_image"]["sha256"]:
        raise SystemExit("USB image SHA-256 does not match package manifest")
    if len(ota) != package_manifest["ota_image"]["bytes"]:
        raise SystemExit("OTA image size does not match package manifest")
    if sha256(ota) != package_manifest["ota_image"]["sha256"]:
        raise SystemExit("OTA image SHA-256 does not match package manifest")

    parts = []
    for label, offset, forced_size in PART_ORDER:
        size = forced_size or package_manifest["parts"][hex(offset)]["bytes"]
        data = image[offset:offset + size]
        if len(data) != size:
            raise SystemExit(f"{label} slice is short: {len(data)} != {size}")
        if label not in {"partitions", "nvs"}:
            recorded = package_manifest["parts"][hex(offset)]
            if recorded["bytes"] != size:
                raise SystemExit(f"{label} size differs from package: {recorded['bytes']} != {size}")
            if recorded["sha256"] != sha256(data):
                raise SystemExit(f"{label} SHA-256 differs from package")
        if label == "app" and data != ota:
            raise SystemExit("app slice does not equal the OTA image")
        if label == "nvs" and data != b"\xff" * size:
            raise SystemExit("NVS slice is not blank")
        parts.append({"label": label, "offset": offset, "size": size,
                      "sha256": sha256(data), "md5": md5(data)})

    if parts[-1]["offset"] + parts[-1]["size"] != len(image):
        raise SystemExit("parts do not cover the full USB image")

    stub_path = site / "vendor" / "metalio" / "esp32s31-stub.json"
    tool_path = site / "vendor" / "metalio" / "esptool.js"
    stub_sha = sha256(stub_path.read_bytes())
    tool_sha = sha256(tool_path.read_bytes())

    usb_rel = f"firmware/metalio/{usb_name}"
    ota_rel = f"firmware/metalio/{ota_name}"
    (site / "firmware" / "metalio").mkdir(parents=True, exist_ok=True)
    (site / usb_rel).write_bytes(image)
    (site / ota_rel).write_bytes(ota)

    release = {
        "schema": 2,
        "name": "ThingsOS Metalio",
        "version": version,
        "project": "ThingsOS_native",
        "board": "Metalio-E-Ink4-Plus",
        "device": "Metalio E-Ink4-Plus",
        "chip": "ESP32-S31",
        "layout": "thingsos-metalio-single-v1",
        "status": "experimental-not-hardware-qualified",
        "firmware_ready": False,
        "previous_version": None,
        "changes": CHANGES,
        "packaged_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "usb": {"path": usb_rel, "size": len(image), "sha256": sha256(image)},
        "ota": {"path": ota_rel, "size": len(ota), "sha256": sha256(ota)},
        "parts": parts,
        "stub": {"path": "vendor/metalio/esp32s31-stub.json", "sha256": stub_sha, "version": "esptool 5.5.0"},
        "tool": {"name": "esptool-js", "version": "0.7.0", "path": "vendor/metalio/esptool.js", "sha256": tool_sha},
        "device_validation": "not performed",
        "limitations": LIMITATIONS,
    }

    manifest = {
        "name": "ThingsOS Metalio",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S31",
                    "parts": [{"path": usb_rel, "offset": 0}]}],
        "downloads": [
            {"kind": "usb", "path": usb_rel, "size": len(image), "sha256": sha256(image)},
            {"kind": "ota", "path": ota_rel, "size": len(ota), "sha256": sha256(ota)},
        ],
    }

    out = site / "metalio"
    out.mkdir(parents=True, exist_ok=True)
    (out / "release.json").write_text(json.dumps(release, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(f"release {version}")
    print(f"USB   {len(image)}  {sha256(image)}")
    print(f"OTA   {len(ota)}  {sha256(ota)}")
    for part in parts:
        print(f"{part['label']:<11} {part['offset']:>10} {part['size']:>9}  {part['sha256']}")


if __name__ == "__main__":
    main()
