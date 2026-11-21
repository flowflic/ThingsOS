# Third-party notices / 第三方许可

This is an independent reader firmware for MindReset's Read Pico board. It is
based on the [MindReset demo firmware](https://github.com/MindReset/read_pico_firmware),
which is licensed under Apache-2.0. The original copyright and license notices
remain in the source. This project is not an official MindReset release.

本项目是基于 MindReset Read Pico 官方示例固件开发的独立阅读固件，并非 MindReset
官方发布。原项目的版权声明和 Apache-2.0 许可均予保留。

| Material / 组件 | License / 许可 | Notice / 声明 |
| --- | --- | --- |
| MindReset Read Pico firmware and board components | Apache-2.0 | [LICENSE](LICENSE); source file headers |
| [Lucide](https://lucide.dev/icons/) — the whole UI icon set, vendored as SVG sources in `tools/icons/lucide/` and compiled into `main/assets/ui_icons.h` by `tools/gen_ui_icons.py` | ISC | [Lucide notice](flash/licenses/LUCIDE-ISC.txt) |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) — progressive JPEG decoding, vendored and patched in `components/jpegdec/` | Apache-2.0 | [Component license](components/jpegdec/LICENSE), [Fork notes](components/jpegdec/FORK.md) |
| [epdiy](https://github.com/vroland/epdiy), including the locally modified driver | LGPL-3.0-or-later | [Modification notes](components/epdiy/LICENSE), [LGPLv3](licenses/LGPL-3.0.txt), [GPLv3](licenses/GPL-3.0.txt) |
| [pypinyin](https://github.com/mozillazg/python-pinyin) dictionary data used by offline book search | MIT | [pypinyin notice](components/read_pico_search/LICENSE.pypinyin) |
| [CrossMux](https://github.com/0x1abin/crossmux) BLE report mapping and text-turn adaptations | MIT | [CrossMux notice](licenses/CrossMux-MIT.txt) |
| FreeInk SDK text-turn waveform assembly, commit `96de1be6ce08eb732909e6e8149af8f892b9a2c5` | MIT | [FreeInk SDK notice](licenses/FreeInk-SDK-MIT.txt) |
| Embedded Noto Sans SC Medium font subset and compressed common-Han bitmaps | SIL OFL-1.1 | [Font license](main/assets/OFL-Noto.txt) |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) browser flasher | Apache-2.0 | [Bundled web tool license](flash/vendor/LICENSE) |
| Espressif TinyUSB component | Apache-2.0 | [Component license](components/espressif__esp_tinyusb/LICENSE) |

License terms apply to their respective material. The repository's top-level
Apache-2.0 license does not replace those component licenses.

各组件继续适用各自的许可，不能把整个固件简称为其中某一个组件的许可。

阅读翻页适配参考 CrossMux `1512e8e049f2b1a4efc7c004a5b1a93cbc5e3f9f`
及其 FreeInk SDK `96de1be6ce08eb732909e6e8149af8f892b9a2c5`。
`components/e0470_epaper_waveform/e0470_epaper_waveform.c` 中文字翻页表的组装移植自
FreeInk `libs/display/EpdiyLcd/src/e0470/e0470_epaper_waveform.c`，保留原有
`Copyright 2026 mindreset` / Apache-2.0 文件头；FreeInk `Copyright (c) 2026 FreeInk`
及 CrossMux MIT 全文分别保留在上述 LICENSE-FreeInk-SDK.txt 和 LICENSE-CrossMux.txt。
仅普通文字翻页启用此表，原始波形数据表、驱动扫描时序与其他界面保持原样。
The text-turn assembly is adapted from the pinned FreeInk source above,
retaining its mindreset Apache-2.0 file notice and the complete FreeInk/CrossMux
MIT notices linked above. Only ordinary text turns select it; vendor LUTs,
scan timing and other screens are unchanged. Panel validation remains required.

The firmware bundles only the embedded Noto Sans SC subset and common-Han bitmap supplement; users may supply
their own fonts on a TF card. The repository and flashing site do not provide
additional font packages.

BLE 蓝牙翻页器的报告解析与按键映射参考 CrossMux 的 BleKeyboardHost，保留组件头部来源说明与 CrossMux MIT 许可。JPEGDEC 使用 BitBank Apache-2.0，具体适配与修正见 `components/jpegdec/FORK.md`。


## ThingsOS Keyboard Sans

Keyboard text uses a renamed static weight-400 subset of the official Read Pico
ChillDuanSansVF font (ChillType / Figtree / Adobe), under SIL OFL 1.1.
The subset includes printable ASCII, keyboard labels and every native Pinyin
candidate. Copyright and license: `native/firmware/main/assets/OFL-Keyboard-Sans.txt`.
Source identity and modifications: `native/firmware/main/assets/keyboard-sans.json`.


## TinyUSB USB device stack

Unmodified TinyUSB 0.21.0~2, Espressif repository commit
`894ea01409e0407a7dbe0ee29b2d58a4691f9046`, under MIT.
Selected library files match the component package checksums.
License and pinned file inventory: `native/vendor/tinyusb/LICENSE` and
`native/vendor/tinyusb/upstream.json`. ThingsOS independently implements
SD ownership, MSC callbacks and UI; no legacy application USB code is used.

## GBK text conversion

The dense GBK/CP936 mapping is adapted from the FatFs table in the pinned
ESP-IDF checkout. The original ChaN license and table provenance are in
`native/vendor/fatfs-txt/LICENSE.txt` and `native/vendor/fatfs-txt/SOURCE.json`.
The TXT provider does not change the filesystem codepage.

## WeRead offline reading time

The offline timing protocol adapter retains its own AGPL/GPL notices. See
`licenses/READTIME-SOURCES.md` for the original authors, revisions, license
texts, and corresponding-source obligations for distribution. The remaining
ThingsOS source retains its existing notices.
