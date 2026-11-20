# ThingsOS handoff — Weidu fix test and open bugs
Date: 2026-10-31
Workspace: D:\ThingsOS_nologotech

## Where things stand
- Device is running the Weidu connection-reuse fix, version 0.1.1.
- App: build/native-idf/ThingsOS_native.bin, 6,499,728 B, SHA-256 1888729f69b26f2688cae374bdeb06cc77ad1b5b14cdff863d08e252e074875d, ELF 8fd9cd4d69e56bf269e8db621fc71fd4e6564210c8c9a1cf448c1cb549751bd8.
- Installed by build/weidu-parallel-20261031/install_parallel_fix.py into build/weidu-parallel-20261031/device-install-2/. result.json has every gate true; main-boot.log shows App version 0.1.1, ELF 8fd9cd4d6..., UI ready: library, Startup health at 35,004 ms, no error lines.
- Host tests: 42/42.
- The capture was stopped after the second freeze: PID 37720 wrote `build/weidu-parallel-fix-capture-2.log` (3,422 bytes; only `ThingsOS_touch` lines, no weidu/TOC lines). The device was reset; `build/weidu-parallel-20261031/recovery-boot.log` is healthy (app loaded at 0x220000, UI ready at 5 s, startup health at 35 s). The previous PID 30036 log is `build/weidu-parallel-fix-capture.log` (2,615 bytes, 16:36, no matching lines).
- The user reported a second freeze at "正在读取章节目录" while caching 《追风筝的人》 (m00850/m00858); the device is recovered. The capture showed only touch events, so the TOC stage produced no weidu log.

## Immediate next steps
1. The capture is stopped; inspect `build/weidu-parallel-fix-capture-2.log` and the freeze at the TOC. Do not start a capture until the user tests again.
2. Analyze the freeze: `build/weidu-parallel-fix-capture-2.log` has no weidu/TOC lines; check why the TOC stage produced no app log and why the UI stopped responding. The recovery log is `build/weidu-parallel-20261031/recovery-boot.log`.
   - Reading page: `Transport stage=reader ... reused=1`; reader around 5 s; no 60 s connect; no repeated `UI IO wait=15000`.
   - Prefetch: two reader workers should show concurrent/alternating reader requests, both with `reused=1`; tokens must be consumed in order. The prefetch path has no explicit start log, so use request interleaving and total time.
   - Speed: compare 5 and 20+ chapter wall time with the old ~5 s/chapter; the shards are ~0.2 s each.
   - TOC: attempts 0/1 use 5 s; attempt 2 uses 15 s; a successful TOC should be about 14–16 s, with bad edges costing about 5 s each.
   - Memory/fallback: if only one reader or old speed, check internal heap and whether prefetch_start fell back to the serial reader; the cache-end log reports DMA/PSRAM.
3. If the reading page freezes again or reader shows `reused=0`, stop the capture, reset with build/weidu-parallel-20261031/reset_recover.py, and investigate. The current fix is in native/firmware/components/thingsos_weidu/weidu_device.c: global download_connection; prefetch_conn_t passed through fetch context; cookie_lock; attempt-based TOC timeout.
4. If the fix works, report the 5/20+ measurements. Do not tune or bump the version without user approval. A new flash requires explicit authorization; the O1-1 approval covered only the current fix flash.
5. Then address the open items below.

## Weidu background (do not redo)
- User reported a 100+ chapter cache taking about 10 min. Diagnostic logs: build/weidu-psvts-capture.log and build/weidu-speed-capture.log. Findings: reader ~5 s/chapter, ~190 KB page, ~38 KB/s; the psvts token sits at the page end and changes every chapter, so token reuse / skipping the reader page is not viable; three shard requests total ~0.6 s; TOC 44 s was two bad-edge 15 s connect failures plus one 14.3 s success.
- O1-2 (two prefetch reader workers + TOC first attempts 5 s) was built and flashed as app efcbef06... / ELF 3f02e6a8.... It froze the reading page: the `_Thread_local download_connection` broke cross-task reuse; reader `reused=0` for 60,796 ms; `UI IO wait=14999/15000`; `Reading completed action=8 error=1`. The TOC 5 s attempts did work. Evidence: build/weidu-parallel-capture.log. The user recovered by reset and chose O1-1.
- The current fix keeps the coordinator/reading path on the global connection and gives each prefetch worker its own connection via a prefetch_conn_t passed in fetch()'s context. The prefetch is minimal: two 12,288-byte tasks, ring of four ordered slots, prefetch_t in internal RAM, fallback to the serial reader if tasks or memory are unavailable.
- Main files: native/src/weidu.c (pre-scan/resume/pending, prefetch start/get/stop), native/include/thingsos/weidu.h (attempt and prefetch callbacks), native/firmware/components/thingsos_weidu/weidu_device.c (fetch, TOC timeout, connection selection, cookie mutex, prefetch workers). The TOC timeout uses client->io.attempt set by request() in native/src/weidu.c.

## Open items
### IMA recording upload failure (highest remaining user bug)
- User report: uploading a recording shows "ima 请求失败，请检查网络或连接配置".
- The generic text comes from native/src/ima_protocol.c:65 call() when the RPC result is NULL/unparseable; it overwrites the specific transport/HTTP messages set by rpc() in native/firmware/components/thingsos_ima/ima_rpc.inc. The actual failing RPC/phase is unknown; no device log was captured.
- Flow: native/firmware/main/ima_ui.inc ima_send -> native/firmware/components/thingsos_ima/ima.c thingsos_ima_upload -> native/src/ima_protocol.c thingsos_ima_execute: check_repeated_names -> create_media -> COS PUT -> add_knowledge -> reconcile. media_type 15. The 2026-10-30 credential-time fix was accepted ("可以了"); the 2026-10-31 bt-ima-fix build was flashed, but the failure was reported after that.
- Next: preserve and display the specific error from rpc()/call(); log the failing phase, HTTP status, and API code; retry with a serial capture. Check the installed IMA docs for check_repeated_names media_type 15 (the docs list 1/3/4/5/7/9/13/14 only). Do not change PMU VCOM or the charger policy.
- References: native/docs/IMA-RECORDING-UPLOAD.md, native/docs/IMA-CREDENTIAL-TIME-AND-WEREAD-PREPARE-20261030.md, native/docs/IMA-AUTH-AND-WEREAD-JPEG-CRASH-20261030.md.

### Weidu TOC ghost regression test
- User saw the initial "正在读取章节目录" line remain under the real chapter titles.
- The UI fix in native/firmware/main/weidu_ui.inc treats both "正在准备微读服务" and "正在读取章节目录" as initial-stage completions. Host test native/tests/device_ui_test.c:1335-1345 passes, but the negative test also passes because app.weidu_covers_redraw makes app.dirty true and progress_only false.
- Before the transition, clear app.weidu_covers_redraw=false and app.weidu_covers_pending=false; touch the test and sources because .inc dependency tracking is stale; rebuild and verify the negative fails and positive passes. Device visual check is still pending.

### Other
- The diagnostic capture showed 14 reader pages while the user said 5 chapters; likely the book has 14 chapters or the queue had more. Unresolved.
- Do not commit. The worktree has many uncommitted user/agent changes; inspect git status before assuming ownership. Version stays 0.1.1 unless the user explicitly requests a bump.

## Build / flash / capture
- Firmware: `powershell -NoProfile -ExecutionPolicy Bypass -Command 'Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; & "D:\ThingsOS_nologotech\native\build.ps1"'`. Git Bash re-injects MSYSTEM; native/build.ps1 handles the rest.
- Host tests: from build/, `powershell -NoProfile -Command "& '.\host-test-incremental.cmd'"`; currently 42/42. Touch all source/test/main .c files after header or .inc changes because the dependency scan is unreliable.
- Installer: `D:\crossmux\.tools\idf-tools\python_env\idf6.1_py3.12_env\Scripts\python.exe build/weidu-parallel-20261031/install_parallel_fix.py`. It asserts the baseline/current app, backs up 16 MiB, boots rescue for 45 s, snapshots NVS, writes main at 0x220000 with dio/80m/16MB, full readback, writes selector at 0xf000, reads it back, boots and checks health. Do not skip gates.
- Capture: `python build/capture-weidu-speed.py COM5 <log> <seconds>`; passive, 115200. The capture is stopped; the last log is `build/weidu-parallel-fix-capture-2.log` (3,422 B).
- Reset: build/weidu-parallel-20261031/reset_recover.py (HardReset, no flash).
- Device procedure: native/docs/METALIO-USB-FLASH-PROCEDURE.md. It covers the S31 ROM/stub sequence, USBJTAGSerialReset, watchdog registers, the USB serial identity, and the full backup/rescue/readback/selector order. Device identity and MAC are recorded there; do not guess them.

## Artifact index
- native/docs/METALIO-PORT.md
- native/docs/METALIO-USB-FLASH-PROCEDURE.md
- native/docs/IMA-RECORDING-UPLOAD.md
- native/docs/IMA-CREDENTIAL-TIME-AND-WEREAD-PREPARE-20261030.md
- native/docs/IMA-AUTH-AND-WEREAD-JPEG-CRASH-20261030.md
- build/weidu-parallel-20261031/install_parallel_fix.py
- build/weidu-parallel-20261031/device-install-2/result.json
- build/weidu-parallel-20261031/device-install-2/main-boot.log
- build/weidu-parallel-fix-capture-2.log (freeze, 3,422 B, only touch)
- build/weidu-parallel-fix-capture.log (old, 2,615 B, 16:36, no matching Transport/Reader lines)
- build/weidu-parallel-20261031/recovery-boot.log (current recovery)
- build/weidu-parallel-20261031/recovery-boot-o1-2.log (previous O1-2 recovery)
- build/weidu-parallel-capture.log (O1-2 freeze)
- build/weidu-psvts-capture.log, build/weidu-speed-capture.log (diagnostic)
- native/src/weidu.c, native/src/weidu_protocol.c, native/include/thingsos/weidu.h
- native/firmware/components/thingsos_weidu/weidu_device.c
- native/firmware/main/weidu_ui.inc, native/tests/device_ui_test.c
- native/src/ima_protocol.c, native/firmware/components/thingsos_ima/ima.c
- native/firmware/components/thingsos_ima/ima_rpc.inc, native/firmware/main/ima_ui.inc
- AGENTS.md

## Suggested skills
Use the Skill tool for:
- writing-plans — C:\Users\freed\.pi\agent\skills\writing-plans\SKILL.md; use before planning the remaining multi-step fixes (IMA error propagation, ghost test isolation, any Weidu tuning).
- agent-reach — C:\Users\freed\.pi\agent\skills\agent-reach\SKILL.md; only if researching IMA/WeRead API error codes or upstream behavior.
- handoff — C:\Users\freed\.pi\agent\skills\handoff\SKILL.md; when producing the next handoff.
Do not load browser, design, or Lark skills for this firmware task.
