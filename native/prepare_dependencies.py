"""Verify pinned generic dependencies offline. Sources and licenses remain intact."""
from pathlib import Path
import hashlib, json
native = Path(__file__).resolve().parent
records = json.loads((native / 'vendor/sources.json').read_text(encoding='utf-8'))
for record in records:
    root = native.parent / record['root']
    for name, digest in record['files'].items():
        path = root / name
        if not path.is_file(): raise SystemExit(f'Missing pinned upstream file: {path}; see vendor/sources.json')
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest: raise SystemExit(f'Upstream file changed: {path}')
    print(f"Verified {record['name']}: {len(record['files'])} pinned files")

# Independently selected, unmodified TinyUSB library; never use legacy USB code.
import json as _usb_json,hashlib as _usb_hashlib
_usb_root=Path(__file__).resolve().parent/'vendor/tinyusb'
_usb_meta=_usb_json.loads((_usb_root/'upstream.json').read_text(encoding='utf-8'))
for _entry in _usb_meta['files']:
 assert _usb_hashlib.sha256((_usb_root/_entry['path']).read_bytes()).hexdigest()==_entry['hash'],_entry['path']
print('Verified TinyUSB:',len(_usb_meta['files']),'pinned files')
