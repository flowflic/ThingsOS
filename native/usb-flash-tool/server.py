#!/usr/bin/env python3
"""Local USB flashing helper for the Metalio E-Ink4-Plus (ESP32-S31).

The page in this directory is served on 127.0.0.1 and asks this process to:

1. connect to ESP32-S31 over the USB-Serial/JTAG port,
2. write the main application to 0x220000,
3. optionally refresh the boot selector at 0xf000,
4. restart the device and read the boot log until the startup health line
   appears, then report which checks passed.

The browser never opens the serial port. Run this with the ESP-IDF Python that
provides esptool 5.5+ and pyserial, for example:

    D:\\crossmux\\.tools\\idf-tools\\python_env\\idf6.1_py3.12_env\\Scripts\\python.exe server.py
"""

from __future__ import annotations

import argparse
import codecs
import ctypes
import hashlib
import json
import re
import struct
import sys
import threading
import time
import traceback
import webbrowser
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

TOOL_DIR = Path(__file__).resolve().parent
ROOT = TOOL_DIR.parents[1]
BUILD = ROOT / "build"

try:
    import esptool
    import serial
    import serial.tools.list_ports
    from esptool.cmds import attach_flash, read_flash, run_stub, write_flash
    from esptool.reset import HardReset, USBJTAGSerialReset
    from esptool.targets.esp32s31 import ESP32S31ROM
except ImportError as exc:  # pragma: no cover - startup diagnosis only
    sys.exit(
        f"缺少依赖：{exc}\n请用 ESP-IDF 的 Python 运行，例如：\n"
        "D:\\crossmux\\.tools\\idf-tools\\python_env\\idf6.1_py3.12_env\\Scripts\\python.exe server.py"
    )

APP_ADDRESS = 0x220000
APP_PARTITION_SIZE = 0xAE0000
SELECTOR_ADDRESS = 0xF000
SELECTOR_SIZE = 0x2000
SELECTOR_SHA256 = "7c0e7928b9f8d90fd5b0d4dcf7bf325674bf4367576345e4789c032e5475741f"
TARGET_CHIP_ID = 32
TARGET_VID_PID = (0x303A, 0x1001)
CAPTURE_SECONDS = 60
HEALTH_LINE = "Startup health confirmed: display, font, touch"
UI_READY_LINE = "UI ready: library"
BOOT_OFFSET_LINE = "Loaded app from partition at offset 0x220000"
FAILURE_MARKERS = (
    "Guru Meditation",
    "assert failed",
    "invalid segment",
    "Panel draw error",
    "underrun occurred",
)
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
PERCENT = re.compile(r"(\d{1,3})\s*%")

JOB_LOCK = threading.Lock()
CURRENT_JOB: "Job | None" = None


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def esptool_version() -> str:
    return getattr(esptool, "__version__", "unknown")


class LineWriter:
    """Capture esptool console output and forward it to the job stream."""

    def __init__(self, job: "Job"):
        self.job = job
        self.buf = ""

    def write(self, text: str) -> None:
        self.buf += text.replace("\r", "\n")
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            line = ANSI.sub("", line).strip()
            if not line:
                continue
            if "Hash of data verified" in line:
                self.job.log(line)
            elif PERCENT.search(line):
                self.job.emit("progress", line)
            else:
                self.job.log(line)

    def flush(self) -> None:
        return None


if sys.platform == "win32":
    from serial import win32

    class StableUSBSerial(serial.Serial):
        """Keep modem lines untouched while only timeouts are reconfigured."""

        keep_line_state = False

        def _reconfigure_port(self):
            if not self.keep_line_state:
                return super()._reconfigure_port()
            timeouts = win32.COMMTIMEOUTS()
            if self._timeout == 0:
                timeouts.ReadIntervalTimeout = win32.MAXDWORD
            elif self._timeout is not None:
                timeouts.ReadTotalTimeoutConstant = max(int(self._timeout * 1000), 1)
            if self._timeout != 0 and self._inter_byte_timeout is not None:
                timeouts.ReadIntervalTimeout = max(
                    int(self._inter_byte_timeout * 1000), 1
                )
            if self._write_timeout == 0:
                timeouts.WriteTotalTimeoutConstant = win32.MAXDWORD
            elif self._write_timeout is not None:
                timeouts.WriteTotalTimeoutConstant = max(
                    int(self._write_timeout * 1000), 1
                )
            if not win32.SetCommTimeouts(self._port_handle, ctypes.byref(timeouts)):
                raise serial.SerialException("SetCommTimeouts failed")
else:  # pragma: no cover - this board is flashed from Windows
    StableUSBSerial = serial.Serial


def open_serial(device: str) -> "serial.Serial":
    port = StableUSBSerial(port=None, baudrate=115200, timeout=0.05, write_timeout=1)
    port.dtr = False
    port.rts = False
    port.port = device
    port.open()
    return port


def list_ports() -> list[dict]:
    found = []
    for info in serial.tools.list_ports.comports():
        vid = info.vid
        pid = info.pid
        found.append(
            {
                "device": info.device,
                "description": info.description or "",
                "vid": f"{vid:04x}" if vid else "",
                "pid": f"{pid:04x}" if pid else "",
                "serialNumber": info.serial_number or "",
                "isTarget": (vid, pid) == TARGET_VID_PID,
            }
        )
    found.sort(key=lambda item: (not item["isTarget"], item["device"]))
    return found


def find_device(preferred: str | None, serial_number: str | None) -> str | None:
    ports = serial.tools.list_ports.comports()
    if serial_number:
        for info in ports:
            if info.serial_number == serial_number:
                return info.device
    if preferred:
        for info in ports:
            if info.device == preferred:
                return info.device
    for info in ports:
        if (info.vid, info.pid) == TARGET_VID_PID:
            return info.device
    return None


def inspect_app_image(path: Path) -> dict:
    data = path.read_bytes()
    if not data:
        raise ValueError("镜像文件为空")
    if data[0] != 0xE9:
        raise ValueError("不是 ESP 应用镜像（缺少 0xE9 头）")
    if len(data) < 112:
        raise ValueError("镜像长度不足，无法读取应用描述")
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != TARGET_CHIP_ID:
        raise ValueError(f"镜像 chip id 为 {chip_id}，不是 ESP32-S31 的 {TARGET_CHIP_ID}")
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("缺少应用描述符")
    if len(data) > APP_PARTITION_SIZE:
        raise ValueError(f"镜像 {len(data)} 字节超过 ota_0 分区大小 {APP_PARTITION_SIZE}")
    return {
        "path": str(path),
        "label": rel(path),
        "size": len(data),
        "mtime": datetime.fromtimestamp(path.stat().st_mtime).strftime(
            "%Y-%m-%d %H:%M:%S"
        ),
        "sha256": hashlib.sha256(data).hexdigest(),
        "version": data[48:80].split(b"\0")[0].decode(errors="replace"),
        "project": data[80:112].split(b"\0")[0].decode(errors="replace"),
    }


def artifact_candidates() -> list[Path]:
    candidates = []
    main = BUILD / "native-idf" / "ThingsOS_native.bin"
    if main.is_file():
        candidates.append(main)
    candidates.extend(sorted((BUILD / "native-usb").glob("*-ota.bin")))
    return candidates


def list_artifacts() -> list[dict]:
    items = []
    for path in artifact_candidates():
        try:
            items.append(inspect_app_image(path))
        except (ValueError, OSError):
            continue
    items.sort(key=lambda item: item["mtime"], reverse=True)
    return items


class Job:
    def __init__(
        self, port: str, serial_number: str, image: Path, write_selector: bool
    ) -> None:
        self.port = port
        self.serial_number = serial_number
        self.image = image
        self.write_selector = write_selector
        self.events: list[dict] = []
        self.seq = 0
        self.running = True
        self.serial = None
        self.result: dict | None = None
        self.lock = threading.Lock()
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        self.evidence = BUILD / "usb-flash-tool" / stamp
        self.evidence.mkdir(parents=True, exist_ok=True)
        self.run_log = (self.evidence / "run.log").open("a", encoding="utf-8")

    def emit(self, kind: str, text: str, level: str = "info") -> None:
        with self.lock:
            self.seq += 1
            self.events.append(
                {"seq": self.seq, "kind": kind, "text": text, "level": level}
            )
            if kind in ("log", "stage"):
                self.run_log.write(f"{time.strftime('%H:%M:%S')} {text}\n")
                self.run_log.flush()

    def log(self, text: str, level: str = "info") -> None:
        self.emit("log", text, level)

    def stage(self, text: str) -> None:
        self.emit("stage", text)

    def state(self, since: int) -> dict:
        with self.lock:
            return {
                "events": [e for e in self.events if e["seq"] > since],
                "seq": self.seq,
                "running": self.running,
                "result": self.result,
            }

    def finish(self, status: str, message: str, checks: list[dict], image: dict) -> None:
        with self.lock:
            self.result = {
                "status": status,
                "message": message,
                "checks": checks,
                "image": image,
                "evidence": rel(self.evidence),
            }
            self.running = False
        payload = {"message": message, "checks": checks, "image": image}
        (self.evidence / "result.json").write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        self.run_log.close()

    # ------------------------------------------------------------------ flow

    def execute(self) -> None:
        image: dict = {}
        checks: list[dict] = []
        expected = (ValueError, RuntimeError, serial.SerialException)
        try:
            self.stage("检查镜像")
            image = inspect_app_image(self.image)
            self.log(f"镜像 {image['label']}")
            self.log(
                f"大小 {image['size']} 字节，版本 {image['version']}，项目 {image['project']}"
            )
            self.log(f"SHA-256 {image['sha256']}")
            self.log(f"目标地址 0x{APP_ADDRESS:x}，串口 {self.port}")
            if image["project"] != "ThingsOS_native":
                raise ValueError(f"镜像项目名为 {image['project']}，不是 ThingsOS_native")
            checks.append({"name": "镜像可用", "ok": True, "detail": image["label"]})

            self.stage("连接设备")
            port, esp = self.connect()
            self.log(f"已连接 {esp.CHIP_NAME}（chip id {TARGET_CHIP_ID}）")
            checks.append(
                {"name": "设备为 ESP32-S31", "ok": True, "detail": esp.CHIP_NAME}
            )

            self.stage("写入主应用")
            data = self.image.read_bytes()
            write_flash(
                esp,
                [(APP_ADDRESS, data)],
                flash_mode="dio",
                flash_freq="80m",
                flash_size="16MB",
                compress=True,
            )
            self.log("读取 0x220000 区间的 MD5")
            remote = esp.flash_md5sum(APP_ADDRESS, len(data))
            if remote != hashlib.md5(data).digest():
                raise RuntimeError("主应用回读 MD5 与镜像不一致")
            checks.append(
                {
                    "name": "主应用写入",
                    "ok": True,
                    "detail": f"0x220000，回读 MD5 {remote.hex()}",
                }
            )

            if self.write_selector:
                detail = self.refresh_selector(esp)
                checks.append({"name": "启动选择", "ok": True, "detail": detail})

            self.stage("复位并读取启动日志")
            self.reset_and_capture(port)
            log_text = self.boot_text
            checks.extend(self.verify_boot(log_text, image))

            failed = [c for c in checks if not c["ok"]]
            if failed:
                detail = "；".join(c["name"] for c in failed)
                self.finish("fail", f"启动检查未通过：{detail}", checks, image)
            else:
                self.finish("ok", "烧录完成，启动健康检查通过", checks, image)
        except Exception as exc:
            self.log(f"{type(exc).__name__}: {exc}", "error")
            if not isinstance(exc, expected):
                for line in traceback.format_exc().splitlines():
                    self.log(line, "error")
            checks.append({"name": "执行", "ok": False, "detail": f"{exc}"})
            self.finish("fail", f"失败：{exc}", checks, image)
        finally:
            if self.serial is not None:
                try:
                    self.serial.close()
                except Exception:
                    pass
                self.serial = None

    def connect(self):
        try:
            port = open_serial(self.port)
        except serial.SerialException as exc:
            raise RuntimeError(f"无法打开串口 {self.port}：{exc}") from exc
        self.serial = port
        rom = ESP32S31ROM(port=port)
        rom._port.keep_line_state = True
        rom._port.write_timeout = 1
        USBJTAGSerialReset(rom._port)()
        boot = bytearray()
        deadline = time.monotonic() + 0.7
        while time.monotonic() < deadline:
            boot.extend(rom._port.read(4096))
            if b"waiting for download" in boot:
                break
        rom.sync()
        # ESP32-S31 LP WDT / SWD registers; volatile download state only.
        rom.write_reg(0x20801018, 0x50D83AA1)
        rom.write_reg(0x20801000, 0)
        rom.write_reg(0x20801018, 0)
        rom.write_reg(0x20801020, 0x50D83AA1)
        rom.write_reg(0x2080101C, rom.read_reg(0x2080101C) | 0x40000)
        rom.write_reg(0x20801020, 0)
        chip_id = rom.get_chip_id()
        if chip_id != TARGET_CHIP_ID:
            raise RuntimeError(f"设备 chip id 为 {chip_id}，不是 ESP32-S31")
        rom._port.write_timeout = 10
        esp = run_stub(rom)
        attach_flash(esp)
        return port, esp

    def refresh_selector(self, esp) -> str:
        desired_path = BUILD / "native-idf" / "ota_data_usb.bin"
        if not desired_path.is_file():
            raise RuntimeError("缺少 build/native-idf/ota_data_usb.bin")
        desired = desired_path.read_bytes()
        digest = hashlib.sha256(desired).hexdigest()
        if len(desired) != SELECTOR_SIZE or digest != SELECTOR_SHA256:
            raise RuntimeError(
                f"启动选择文件与已验证的布局不一致（长度 {len(desired)}，"
                f"SHA-256 {digest}），拒绝写入"
            )
        current = read_flash(
            esp, SELECTOR_ADDRESS, SELECTOR_SIZE, no_progress=True
        )
        if current == desired:
            self.log("启动选择数据已指向主应用，无需重写")
            return "0xf000 已一致"
        self.log("写入启动选择数据 0xf000")
        write_flash(
            esp,
            [(SELECTOR_ADDRESS, desired)],
            flash_mode="dio",
            flash_freq="80m",
            flash_size="16MB",
            compress=True,
        )
        readback = read_flash(
            esp, SELECTOR_ADDRESS, SELECTOR_SIZE, no_progress=True
        )
        if readback != desired:
            raise RuntimeError("启动选择数据回读不一致")
        self.log("启动选择数据回读一致")
        return "0xf000 已写入并回读一致"

    def reset_and_capture(self, port) -> None:
        try:
            HardReset(port, uses_usb=True).reset()
        except (serial.SerialException, OSError):
            pass
        try:
            port.close()
        except Exception:
            pass
        self.boot_text = self.capture()

    def capture(self) -> str:
        deadline = time.monotonic() + CAPTURE_SECONDS
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        collected = ""
        port = None
        next_open = 0.0
        stop_after = None
        self.log(f"捕获启动日志，最多 {CAPTURE_SECONDS} 秒")
        while time.monotonic() < deadline:
            if port is None:
                if time.monotonic() >= next_open:
                    device = find_device(self.port, self.serial_number)
                    if device:
                        try:
                            port = open_serial(device)
                            self.log(f"读取 {device}")
                        except serial.SerialException:
                            port = None
                    next_open = time.monotonic() + 0.2
                if port is None:
                    time.sleep(0.1)
                    continue
            try:
                chunk = port.read(min(port.in_waiting or 1, 16384))
            except (serial.SerialException, OSError):
                try:
                    port.close()
                except Exception:
                    pass
                port = None
                continue
            if chunk:
                text = decoder.decode(chunk)
                if text:
                    collected += text
                    self.emit("device", ANSI.sub("", text))
                if HEALTH_LINE in collected and stop_after is None:
                    stop_after = time.monotonic() + 2.0
            if stop_after is not None and time.monotonic() >= stop_after:
                break
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
        return collected

    def verify_boot(self, text: str, image: dict) -> list[dict]:
        return [
            {
                "name": "加载主应用分区",
                "ok": BOOT_OFFSET_LINE in text,
                "detail": "offset 0x220000",
            },
            {
                "name": "界面就绪",
                "ok": UI_READY_LINE in text,
                "detail": UI_READY_LINE,
            },
            {
                "name": "启动健康",
                "ok": HEALTH_LINE in text,
                "detail": HEALTH_LINE,
            },
            {
                "name": "无崩溃标记",
                "ok": not any(m in text for m in FAILURE_MARKERS),
                "detail": "未发现 Guru Meditation / assert / invalid segment",
            },
        ]


def start_job(payload: dict) -> Job:
    global CURRENT_JOB
    with JOB_LOCK:
        if CURRENT_JOB is not None and CURRENT_JOB.running:
            raise RuntimeError("已有烧录任务在进行")
        port = str(payload.get("port") or "").strip()
        if not port:
            raise ValueError("未选择串口")
        raw_image = str(payload.get("image") or "").strip()
        if not raw_image:
            raise ValueError("未选择镜像")
        image_path = Path(raw_image)
        if not image_path.is_absolute():
            image_path = ROOT / image_path
        if not image_path.is_file():
            raise ValueError(f"找不到镜像文件：{image_path}")
        serial_number = str(payload.get("serialNumber") or "").strip()
        job = Job(
            port=port,
            serial_number=serial_number,
            image=image_path.resolve(),
            write_selector=bool(payload.get("writeSelector", True)),
        )
        CURRENT_JOB = job
    threading.Thread(target=job.execute, name="flash-job", daemon=True).start()
    return job


class Handler(BaseHTTPRequestHandler):
    server_version = "ThingsOSFlash/1.0"

    def log_message(self, fmt, *args):  # keep the console clean
        return None

    def send_bytes(self, body: bytes, content_type: str, status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, data: dict, status: int = 200) -> None:
        self.send_bytes(
            json.dumps(data, ensure_ascii=False).encode("utf-8"),
            "application/json; charset=utf-8",
            status,
        )

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self.send_bytes((TOOL_DIR / "index.html").read_bytes(), "text/html; charset=utf-8")
        elif parsed.path == "/app.js":
            self.send_bytes(
                (TOOL_DIR / "app.js").read_bytes(), "text/javascript; charset=utf-8"
            )
        elif parsed.path == "/styles.css":
            self.send_bytes(
                (TOOL_DIR / "styles.css").read_bytes(), "text/css; charset=utf-8"
            )
        elif parsed.path == "/api/state":
            artifacts = list_artifacts()
            job = CURRENT_JOB
            self.send_json(
                {
                    "python": sys.version.split()[0],
                    "esptool": esptool_version(),
                    "ports": list_ports(),
                    "artifacts": artifacts,
                    "defaultImage": artifacts[0]["label"] if artifacts else "",
                    "captureSeconds": CAPTURE_SECONDS,
                    "busy": bool(job and job.running),
                }
            )
        elif parsed.path == "/api/events":
            query = parse_qs(parsed.query)
            since = int((query.get("since") or ["0"])[0])
            job = CURRENT_JOB
            if job is None:
                self.send_json({"events": [], "seq": 0, "running": False, "result": None})
            else:
                self.send_json(job.state(since))
        else:
            self.send_json({"error": "not found"}, 404)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path != "/api/flash":
            self.send_json({"error": "not found"}, 404)
            return
        length = int(self.headers.get("Content-Length") or 0)
        try:
            payload = json.loads(self.rfile.read(length) or b"{}")
            start_job(payload)
        except Exception as exc:
            self.send_json({"error": str(exc)}, 409)
            return
        self.send_json({"ok": True})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8848, help="HTTP 端口，默认 8848")
    parser.add_argument("--open", action="store_true", help="启动后打开浏览器")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"USB 烧录工具：{url}")
    print(f"esptool {esptool_version()}，Python {sys.version.split()[0]}")
    if args.open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")


if __name__ == "__main__":
    main()
