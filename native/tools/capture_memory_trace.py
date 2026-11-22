# SPDX-License-Identifier: Apache-2.0
"""Listen (read-only) to the Metalio E-Ink4-Plus USB console and record
ThingsOS_memtrace samples to CSV plus a per-scenario summary.

The device firmware prints one ThingsOS_memtrace line every 2 s while USB is
attached, plus event lines (Wi-Fi, BLE, transfer service, WeRead, book open).
This script never writes to the port. DTR/RTS are cleared before opening so
the ESP32-S31 USB-Serial/JTAG is not reset into download mode.

Do not run it while the web flasher or esptool holds the port.
"""
import argparse
import csv
from datetime import datetime
import json
from pathlib import Path
import re
import time

# pyserial is imported lazily (capture only): run capture from the ESP-IDF
# Python environment; --summarize and parsing work without it.

HEADER = re.compile(r'\((\d+)\)\s+ThingsOS_memtrace:\s+(.*)')
# Numeric WeRead breadcrumbs only (never URLs, cookies or titles).
DETAIL = re.compile(r'\(\d+\)\s+ThingsOS_weidu: (?:HTTP (?:GET|POST) status=|Transport stage=|Job peak |'
                    r'Exchange completed action=|Worker stack margin=|DMA free=|'
                    r'Retry link wait restored=|Cancel cleanup elapsed=)[^\r\n\x1b]*')
POOLS = ('sram', 'dma', 'lp', 'psram')
METRICS = tuple(f'{pool}_{field}' for pool in POOLS for field in ('free', 'total', 'largest', 'min'))
FIELDS = ('host_time', 'device_ms', 'event', 'subject', 'result', *METRICS,
          *(f'{pool}_used' for pool in POOLS))
# usb-sample result flags (firmware/main/main.c memory_sample_tick).
FLAGS = {1: 'book', 2: 'transfer', 4: 'weidu', 8: 'ble', 16: 'ble-connected', 32: 'asleep',
         64: 'audio', 128: 'ble-hid'}
WEIDU_ACTIONS = {2: 'cache', 17: 'chapter'}
REGIONS = {0: 'other', 1: 'main-sram', 2: 'lp-ram', 3: 'psram'}


def find_port(serial_number):
    from serial.tools import list_ports
    matches = [p.device for p in list_ports.comports()
               if (p.vid, p.pid) == (0x303a, 0x1001)
               and (serial_number is None or (p.serial_number or '').lower() == serial_number.lower())]
    return matches[0] if len(matches) == 1 else None


def parse(line):
    match = HEADER.search(line)
    if not match:
        return None
    fields = dict(re.findall(r'(\w+)=(-?[\w-]+)', match.group(2)))
    if not {'event', 'subject', 'result', *METRICS}.issubset(fields):
        return None
    row = {'device_ms': int(match.group(1)), 'event': fields['event']}
    row.update({key: int(fields[key]) for key in ('subject', 'result', *METRICS)})
    for pool in POOLS:
        row[f'{pool}_used'] = row[f'{pool}_total'] - row[f'{pool}_free']
    return row


def scenario(row):
    """Map a sample to the P0 scenario it evidences, or None."""
    event, subject, result = row['event'], int(row['subject']), int(row['result'])
    if event in ('wifi-connected', 'http-started', 'http-stopped', 'book-open', 'book-closed',
                 'ble-controller-init', 'ble-controller-ready', 'ble-controller-refused',
                 'ble-started', 'ble-stopped', 'library-ready', 'ble-controller-peak', 'audio-parked'):
        return event
    if event == 'ble-state':
        if result & 4:
            return 'ble-hid-ready'
        if result & 2:
            return 'ble-connected'
        if result & 1:
            return 'ble-scanning'
        return None
    if event.startswith('weidu-') and subject in WEIDU_ACTIONS:
        return f'{event}-{WEIDU_ACTIONS[subject]}'
    if event == 'usb-sample':
        active = [name for bit, name in FLAGS.items() if result & bit and name != 'asleep']
        return 'sample:' + ('+'.join(active) if active else 'idle')
    return None


REQUIRED = {'wifi-connected', 'ble-controller-ready', 'ble-scanning', 'ble-hid-ready',
            'http-started', 'http-stopped', 'book-open', 'book-closed',
            'weidu-worker-chapter', 'weidu-finished-chapter',
            'weidu-worker-cache', 'weidu-finished-cache'}
KEYS = ('sram_free', 'sram_largest', 'dma_free', 'dma_largest', 'lp_free', 'lp_largest', 'psram_free')


def summarize(rows):
    rows = [{k: (int(v) if k not in ('host_time', 'event') and v != '' else v) for k, v in r.items()}
            for r in rows]
    scenes = {}
    for row in rows:
        name = scenario(row)
        if name:
            scenes.setdefault(name, []).append(row)
    out = {}
    for name, items in sorted(scenes.items()):
        out[name] = {'count': len(items),
                     'min': {k: min(r[k] for r in items) for k in KEYS},
                     'last': {k: items[-1][k] for k in KEYS}}
    workers = [r for r in rows if r['event'] == 'weidu-worker']
    stacks = [r for r in rows if r['event'] == 'weidu-stack']
    return {
        'units': 'bytes',
        'samples': len(rows),
        # dma_* is main HP SRAM on ESP32-S31; sram_* also includes LP RAM.
        'minimum_sampled': {k: min(r[k] for r in rows) for k in KEYS} if rows else {},
        'weidu_stack_region': [{'action': r['subject'], 'region': REGIONS.get(r['result'], r['result'])}
                               for r in workers],
        'weidu_stack_margin': [{'action': r['subject'], 'bytes': r['result']} for r in stacks],
        # Peak main-SRAM draw measured with local-minimum windows (bytes).
        'weidu_peak_use': [{'action': r['subject'], 'bytes': r['result']}
                           for r in rows if r['event'] == 'weidu-peak'],
        'ble_controller_init': [{'peak_use': r['subject'], 'steady_use': r['result'],
                                 'main_free_after': r['dma_free'], 'main_largest_after': r['dma_largest']}
                                for r in rows if r['event'] == 'ble-controller-peak'],
        'audio_parked': [{'position_ms': r['result'], 'main_free': r['dma_free'], 'main_largest': r['dma_largest']}
                         for r in rows if r['event'] == 'audio-parked'],
        'missing_scenarios': sorted(REQUIRED - scenes.keys()),
        'scenarios': out,
    }


def capture(out, seconds, stop_file, serial_number):
    import serial
    out.mkdir(parents=True, exist_ok=True)
    data = out / 'memory.csv'
    rows = []
    if data.exists():
        with data.open(newline='', encoding='utf-8') as source:
            rows.extend(csv.DictReader(source))
    coverage = set(summarize(rows)['scenarios']) if rows else set()
    with (data.open('a', newline='', encoding='utf-8') as output,
          (out / 'weidu-serial.log').open('a', encoding='utf-8') as diagnostic,
          (out / 'console.log').open('a', encoding='utf-8') as console):
        writer = csv.DictWriter(output, FIELDS)
        if not rows:
            writer.writeheader()

        def record(line):
            # Full console for failure analysis (firmware never logs URLs,
            # cookies, QR identifiers or credentials). Kept under build/ only.
            console.write(line.rstrip(chr(13)) + chr(10))
            console.flush()
            if details := DETAIL.search(line):
                diagnostic.write(details.group(0) + '\n')
                diagnostic.flush()
            row = parse(line)
            if not row:
                return
            row['host_time'] = datetime.now().astimezone().isoformat(timespec='milliseconds')
            rows.append(row)
            writer.writerow(row)
            output.flush()
            name = scenario(row)
            if name and name not in coverage:
                coverage.add(name)
                print(f'{name}: main SRAM free={row["dma_free"]} largest={row["dma_largest"]}, '
                      f'LP free={row["lp_free"]}, PSRAM free={row["psram_free"]}, result={row["result"]}',
                      flush=True)

        deadline = time.monotonic() + seconds
        conn = None
        pending = bytearray()
        try:
            while time.monotonic() < deadline and not (stop_file and stop_file.exists()):
                if conn is None:
                    device = find_port(serial_number)
                    if device is None:
                        time.sleep(1)
                        continue
                    candidate = serial.Serial(None, 115200, timeout=0.25)
                    candidate.dtr = candidate.rts = False  # before open: no reset
                    candidate.port = device
                    try:
                        candidate.open()
                    except (serial.SerialException, OSError):
                        candidate.close()
                        time.sleep(1)
                        continue
                    conn = candidate
                    print(f'Listening on {device}', flush=True)
                try:
                    pending.extend(conn.read(2048))
                except (serial.SerialException, OSError):
                    conn.close()
                    conn = None
                    pending.clear()
                    continue
                while (end := pending.find(b'\n')) >= 0:
                    record(pending[:end].decode('utf-8', errors='replace'))
                    del pending[:end + 1]
                if len(pending) > 8192:
                    pending.clear()
        finally:
            if conn is not None:
                conn.close()
            result = summarize(rows)
            (out / 'memory-summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n',
                                                     encoding='utf-8')
            print(f'{len(rows)} samples; missing: {", ".join(result["missing_scenarios"]) or "none"}',
                  flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True, help='output directory (use build/...)')
    parser.add_argument('--serial-number', help='USB serial number (device MAC); required if several '
                        'Espressif USB-Serial/JTAG ports are attached')
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--stop-file', type=Path, help='stop when this file appears')
    parser.add_argument('--summarize', action='store_true', help='only rebuild memory-summary.json')
    args = parser.parse_args()
    if args.summarize:
        with (args.out / 'memory.csv').open(newline='', encoding='utf-8') as source:
            result = summarize(list(csv.DictReader(source)))
        (args.out / 'memory-summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n',
                                                      encoding='utf-8')
        print(f'{result["samples"]} samples; missing: {", ".join(result["missing_scenarios"]) or "none"}')
        return
    capture(args.out, args.seconds, args.stop_file, args.serial_number)


if __name__ == '__main__':
    main()
