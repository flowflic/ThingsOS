# Offline WeRead reading time / 离线阅读时长来源

Added to ThingsOS on 2026-11-13. This file identifies adapted implementation
separately from independently authored integration. The repository's original
Apache-2.0 notices do not relicense the copyleft material below.

| Source | Pinned revision and material | Local use | License |
| --- | --- | --- | --- |
| [MiuRead](https://github.com/miumiupy98-art/miuread-koreader) | `418380a0638381fcd8e2bad8d1f23be79fddd7c4`, `miuread.koplugin/miuread/readtime_protocol.lua` | C adaptation of historical-hour DTO, signing inputs and request envelope in `native/src/weidu_readtime_protocol.c` | AGPL-3.0-only; [full text](MiuRead-AGPL-3.0.txt), [upstream notice](MiuRead-NOTICE.txt) |
| [AnkioTomas/moon](https://github.com/AnkioTomas/moon) | `43b2181da6eb5285b62114da85e1413d2f8464e4`, `book.koplugin/source/wechat/app.lua`, `tests/source/wechat/app_spec.lua` | Original signature substitution table/transformation, via MiuRead; public signature test vector | GPL-3.0, [full text](moon-GPL-3.0.txt); these portions retain their GPL terms |
| [weread-omni](https://github.com/teng-lin/weread-omni) | `88bd2e095d7d7ee423eaadf8f40653e72c5be6d4`, `src/auth/qrlogin.ts`, `docs/endpoints.md` | Reference for separate Native E-Ink QR authorization, credentials and endpoint contracts | MIT, Copyright (c) 2026 Teng Lin; [full notice](weread-omni-MIT.txt) |

MiuRead originated from `finlater/weread.koplugin` v0.1.1. Original rights and
MiuRead contributors' additions are acknowledged in the preserved upstream
NOTICE. Its third-party declaration identifies the GPL-covered moon signature
material and the combination under GPLv3/AGPLv3 section 13.

The C protocol module is marked `AGPL-3.0-only`; translating the Lua algorithm
to C does not make that material independently authored Apache-2.0 code.
It changes serialization to cJSON, uses bounded byte arrays with explicit
lengths (including NUL bytes), and accepts explicit times/random values.

The spool, runtime interval callback, firmware transport and UI are new ThingsOS
code. Their behavioral review also used MiuRead `readtime_outbox.lua`,
`eink_readtime.lua`, `readtime_controller.lua` and PR [#187](https://github.com/miumiupy98-art/miuread-koreader/pull/187),
including the [#184 fix](https://github.com/miumiupy98-art/miuread-koreader/commit/eaaa5791d7a93282e90d180ecd1eac2fbc9dfc8f).
They do not incorporate the KOReader or SQLite implementation.

The earlier v6.3.5 reference is
`00877a200b7b025a0a7cd8e3dc864e0638d0201d`. Later corrections were reviewed,
not merely that first release. The inspected beta head
`16c7b26eebebbb567736e01b2f54098a5e3dbc2f` has no differing reading-time module
files relative to the pinned main revision above.

Distribution of firmware incorporating the adapted module must address the
AGPL combined-work and corresponding-source requirements, alongside the
retained licenses of all other components. A source attribution alone does not
replace those obligations. The top-level Apache license continues to apply to
its original material; this feature is not a claim that the combined firmware
is wholly Apache-2.0. No firmware release or publication is part of this change.

No affiliation with, or endorsement by, Tencent, WeRead, MiuRead, moon, or
weread-omni is claimed. No account credentials or upstream application binary
are embedded in the source or notices.
