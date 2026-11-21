"""Extract measured UI timing from existing ThingsOS serial logs, without a device.

Times are firmware log timestamps, not physical touch or optical measurements.
The pre-update residual includes power/temperature, conversion and other work;
it must not be labelled as an isolated framebuffer conversion measurement.
"""
import argparse
import csv
import hashlib
import json
import re
import statistics
from pathlib import Path

PAGES = {0: "书库", 1: "阅读", 8: "设置", 13: "传书", 15: "键盘"}
LINE = re.compile(r"[IWE] \((\d+)\) (.*)")
TAP = re.compile(r"ThingsOS_touch: tap x=(\d+) y=(\d+) page=(\d+)")
LAYOUT = re.compile(r"UI layout page=(\d+): (\d+) ms")
REFRESH = re.compile(r"Refresh (\w+): (\d+) ms, clean=(\d+)")
EPD = re.compile(r"(?:full )?diff: (\d+)ms, draw: (\d+)ms, buffer update: (\d+)ms, total: (\d+)ms")
SCOPE = re.compile(r"GC16 scope rows=(\d+)/(\d+) columns=(\d+)/(\d+) full=(\d+)(?: equal_hold=(\d+))?")
HOME = re.compile(r"Home transition erase=(\d+) us redraw=(\d+) us result=0x([0-9a-f]+)")


def parse(path):
    samples, pending, down, epd = [], None, None, None
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        match = LINE.search(line)
        if not match:
            continue
        time, body = int(match[1]), match[2]
        if "ThingsOS_touch: down " in body:
            down = time
        tap = TAP.search(body)
        if tap:
            x, y, page = map(int, tap.groups())
            pending = dict(log=path.name, tap_line=number, tap_at_ms=time,
                           source_page=page, source=PAGES.get(page, str(page)), x=x, y=y,
                           observed_contact_ms=time-down if down is not None and time >= down else None)
            down, epd = None, None
        layout = LAYOUT.search(body)
        if layout and pending and "layout_ms" not in pending:
            page, duration = map(int, layout.groups())
            pending.update(target_page=page, target=PAGES.get(page, str(page)),
                           layout_line=number, layout_end_ms=time, layout_ms=duration,
                           action_and_wait_ms=time-duration-pending["tap_at_ms"])
        detail = EPD.search(body)
        if detail:
            diff, draw, copy, total = map(int, detail.groups())
            detail_values = dict(diff_ms=diff, waveform_ms=draw, buffer_update_ms=copy, epd_total_ms=total, epd_passes=1)
            epd = {key:value+(epd or {}).get(key,0) for key,value in detail_values.items()}
        home = HOME.search(body)
        if home and pending:
            pending.update(home_erase_us=int(home[1]),home_redraw_us=int(home[2]),home_result=int(home[3],16))
        scope = SCOPE.search(body)
        if scope and pending:
            rows,total_rows,columns,total_columns,full = map(int,scope.groups()[:5])
            pending.update(drive_rows=rows,panel_rows=total_rows,drive_columns=columns,
                           panel_columns=total_columns,scope_full=full,
                           row_column_mask_fraction=rows*columns/(total_rows*total_columns),
                           equal_hold=int(scope[6]) if scope[6] is not None else 0)
        refresh = REFRESH.search(body)
        if refresh and pending and "layout_ms" in pending:
            mode, duration, clean = refresh.groups()
            pending.update(refresh_line=number, refresh_end_ms=time, mode=mode, clean=int(clean),
                           update_ms=int(duration), tap_to_complete_ms=time-pending["tap_at_ms"],
                           pre_update_residual_ms=time-int(duration)-pending["layout_end_ms"])
            if epd:
                pending.update(epd)
            samples.append(pending)
            pending, epd = None, None
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Output path prefix")
    args = parser.parse_args()
    samples = [sample for path in args.logs for sample in parse(path)]
    tabs = [s for s in samples if s["source_page"] in (0, 8, 13)
            and s["target_page"] in (0, 8, 13) and s["source_page"] != s["target_page"]
            and s["y"] >= 1098 and s["action_and_wait_ms"] >= 0
            and s["pre_update_residual_ms"] >= 0]
    groups = {}
    for sample in tabs:
        groups.setdefault(sample["source"] + "→" + sample["target"], []).append(sample)
    stats = {}
    for name, rows in groups.items():
        stats[name] = {"count": len(rows)}
        for key in ("tap_to_complete_ms", "action_and_wait_ms", "layout_ms", "pre_update_residual_ms", "update_ms"):
            values = [s[key] for s in rows]
            stats[name][key] = dict(min=min(values), median=statistics.median(values), max=max(values))
    result = dict(boundary="Log-based observations, not a controlled benchmark or optical settling measurement. Residual is not an isolated conversion measurement. Collection time and firmware version require separate provenance.",
                  logs=[dict(name=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in args.logs],
                  tab_summary=stats, tab_samples=tabs, all_tap_samples=samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix(".json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    keys = sorted({k for row in tabs for k in row})
    with args.output.with_suffix(".csv").open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=keys or ["log"])
        writer.writeheader()
        writer.writerows(tabs)
    print(json.dumps(dict(tab_summary=stats, samples=len(samples), tab_samples=len(tabs)), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
