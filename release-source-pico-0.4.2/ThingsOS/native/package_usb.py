"""Build a complete local USB installation from hash-verified validation inputs.

No serial connection, publication or device acceptance is implied by packaging.
The image initializes NVS/OTA slots; SD books, fonts and wallpapers are separate.
"""
from pathlib import Path
import binascii
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / 'build/native-idf'
OUT = ROOT / 'build/native-usb'


def ota_selector():
    selector = bytearray(b'\xff' * 0x2000)
    crc = binascii.crc32(struct.pack('<I', 1), 0xffffffff) & 0xffffffff
    assert crc == 0x4743989a
    selector[:32] = struct.pack('<I20sII', 1, b'\xff'*20, 0, crc)
    return selector


def package():
    validation = json.loads((BUILD / 'validation.json').read_text(encoding='utf-8'))
    assert validation['build_dependency_boundary_passed'] and validation['boot_guard_linked']
    assert validation['usb_flash_enabled'] and '100% tests passed' in validation['host_tests']
    images = {}
    for item in validation['images']:
        data = Path(item['file']).read_bytes()
        assert len(data) == item['bytes'] and hashlib.sha256(data).hexdigest() == item['sha256'], 'Re-run verify_build.py after building'
        images[int(item['development_offset'], 0)] = data
    assert set(images) == {0, 0x8000, 0x20000, 0x220000, 0xd00000}
    expected = {'rescue': (0x20000, 0x200000), 'ota_0': (0x220000, 0xae0000), 'system_font': (0xd00000, 0x300000),
                'nvs': (0x9000, 0x6000), 'otadata': (0xf000, 0x2000)}
    partitions = {}
    table = images[0x8000]
    for at in range(0, len(table)-31, 32):
        magic, kind, subtype, offset, size, name, flags = struct.unpack_from('<HBBII16sI', table, at)
        if magic != 0x50aa:
            break
        partitions[name.split(b'\0')[0].decode()] = (offset, size)
    assert all(partitions.get(name) == region for name, region in expected.items())
    assert set(partitions)=={"nvs","otadata","phy_init","rescue","ota_0","system_font"}
    main = images[0x220000]
    assert main[0] == 0xe9 and main[80:112].split(b'\0')[0] == b'ThingsOS_native'
    assert images[0x20000][80:112].split(b'\0')[0] == b'ThingsOS_rescue'
    version = main[48:80].split(b'\0')[0].decode()
    images[0xf000] = ota_selector()
    end = max(offset + len(data) for offset, data in images.items())
    assert end <= 0x1000000
    merged = bytearray(b'\xff' * end)
    previous = 0
    for offset, data in sorted(images.items()):
        assert offset >= previous
        merged[offset:offset+len(data)] = data
        previous = offset+len(data)
    assert merged[0x9000:0xf000] == b'\xff' * 0x6000
    assert merged[0x220000+len(main):0xd00000] == b'\xff' * (0xae0000-len(main))
    OUT.mkdir(parents=True, exist_ok=True)
    image = OUT / f'ThingsOS-{version}-usb.bin'
    image.write_bytes(merged)
    manifest = {'version': version, 'chip': 'ESP32-S3', 'offset': 0,
                'file': image.name, 'bytes': len(merged), 'sha256': hashlib.sha256(merged).hexdigest(),
                'initializes_settings_and_ota': True, 'sd_untouched': True,
                'device_validation': 'not performed for this build', 'published': False}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    web = {'name': 'ThingsOS', 'version': version,
           'builds': [{'chipFamily': 'ESP32-S3', 'parts': [{'path': image.name, 'offset': 0}]}]}
    (OUT / 'install-manifest.json').write_text(json.dumps(web, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--ota-selector':
        Path(sys.argv[2]).write_bytes(ota_selector())
    else:
        assert len(sys.argv)==1
        package()
