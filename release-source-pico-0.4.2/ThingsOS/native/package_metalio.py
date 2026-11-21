"""Merge the Metalio E-Ink4-Plus (ESP32-S31) build into one complete USB image.

Reads the IDF flasher_args.json of the main build, which already carries the
S31 bootloader offset (0x2000), the rescue image and the system font. Checks
partition layout, image magic, project names and a common chip id, then writes
build/native-usb/ThingsOS-metalio-<version>-usb.bin plus a SHA-256 manifest.
Packaging never connects to or writes a device.
"""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/native-idf'
OUT = ROOT / 'build/native-usb'
FLASH_BYTES = 0x1000000
EXPECTED_PARTITIONS = {
    'nvs': (0x9000, 0x6000), 'otadata': (0xf000, 0x2000), 'phy_init': (0x11000, 0x1000),
    'rescue': (0x20000, 0x200000), 'ota_0': (0x220000, 0xae0000), 'system_font': (0xd00000, 0x300000),
}


def partitions(table):
    found = {}
    for at in range(0, len(table) - 31, 32):
        magic, _kind, _subtype, offset, size, name, _flags = struct.unpack_from('<HBBII16sI', table, at)
        if magic != 0x50aa:
            break
        found[name.split(b'\0')[0].decode()] = (offset, size)
    return found


def app_project(image):
    # esp_image_header_t (24 B) + first segment header (8 B) precede esp_app_desc_t.
    assert image[0] == 0xe9, 'not an ESP application image'
    desc = image[32:32 + 256]
    assert struct.unpack_from('<I', desc, 0)[0] == 0xABCD5432, 'missing app descriptor'
    version = desc[16:48].split(b'\0')[0].decode()
    project = desc[48:80].split(b'\0')[0].decode()
    return project, version


def main():
    args = json.loads((BUILD / 'flasher_args.json').read_text(encoding='utf-8'))
    assert args['extra_esptool_args']['chip'] == 'esp32s31'
    images = {int(offset, 0): (BUILD / name).resolve().read_bytes()
              for offset, name in args['flash_files'].items()}
    assert set(images) == {0x2000, 0x8000, 0xf000, 0x20000, 0x220000, 0xd00000}, sorted(map(hex, images))

    table = partitions(images[0x8000])
    assert table == EXPECTED_PARTITIONS, table
    chip_ids = {struct.unpack_from('<H', images[o], 12)[0] for o in (0x2000, 0x20000, 0x220000)}
    assert len(chip_ids) == 1, 'bootloader, rescue and main were built for different chips'
    main_project, version = app_project(images[0x220000])
    rescue_project, rescue_version = app_project(images[0x20000])
    assert main_project == 'ThingsOS_native' and rescue_project == 'ThingsOS_rescue'
    assert version == rescue_version, 'main and rescue versions differ'
    assert images[0xd00000][:8] == b'THFONT01'
    for name, (offset, size) in EXPECTED_PARTITIONS.items():
        if offset in images:
            assert len(images[offset]) <= size, f'{name} image exceeds its partition'

    end = max(offset + len(data) for offset, data in images.items())
    assert end <= FLASH_BYTES
    merged = bytearray(b'\xff' * end)
    previous = 0
    for offset, data in sorted(images.items()):
        assert offset >= previous, 'overlapping images'
        merged[offset:offset + len(data)] = data
        previous = offset + len(data)
    # NVS stays erased: a complete install starts with default settings.
    assert merged[0x9000:0xf000] == b'\xff' * 0x6000

    OUT.mkdir(parents=True, exist_ok=True)
    target = OUT / f'ThingsOS-metalio-{version}-usb.bin'
    target.write_bytes(merged)
    ota = OUT / f'ThingsOS-metalio-{version}-ota.bin'
    ota.write_bytes(images[0x220000])
    manifest = {
        'device': 'Metalio E-Ink4-Plus', 'chip': 'esp32s31', 'version': version,
        'usb_image': {'file': target.name, 'bytes': len(merged), 'sha256': hashlib.sha256(merged).hexdigest(),
                      'flash_offset': '0x0'},
        'ota_image': {'file': ota.name, 'bytes': len(images[0x220000]),
                      'sha256': hashlib.sha256(images[0x220000]).hexdigest()},
        'parts': {hex(o): {'bytes': len(d), 'sha256': hashlib.sha256(d).hexdigest()} for o, d in sorted(images.items())},
        'esptool': 'esptool --chip esp32s31 --no-stub -p <PORT> write-flash 0x0 ' + target.name,
        'status': 'development build; device acceptance pending',
    }
    (OUT / 'metalio-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
