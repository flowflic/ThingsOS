"""Verify local serial/write evidence without publishing raw device logs or credentials."""
from datetime import datetime, timezone
import binascii
import hashlib
import json
import re
import struct
from pathlib import Path

build = Path(__file__).resolve().parent.parent / 'build'
main = build / 'native-idf/ThingsOS_native.bin'
rescue = build / 'native-rescue/ThingsOS_rescue.bin'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def log(name):
    return (build / name).read_text(encoding='utf-8', errors='replace')


main_log = log('device-main-restored-0.2.2.log')
touch_log = log('device-touch-main-0.2.2.log')
rescue_log = log('device-rescue-final-0.2.2.log')
for contents, project, elf in (
        (main_log, 'ThingsOS_native', build / 'native-idf/ThingsOS_native.elf'),
        (rescue_log, 'ThingsOS_rescue', build / 'native-rescue/ThingsOS_rescue.elf')):
    assert project in contents and '0.2.2' in contents
    assert digest(elf)[:9] in contents, 'Log belongs to a different ELF'
    assert not any(marker in contents for marker in ('Guru Meditation', 'panic\'ed', 'Rebooting...', 'Board initialization returned'))
    assert 'Display/touch initialized; PMU RUNNING' in contents
assert 'UI ready: library; initial screen update succeeded' in main_log
assert 'Startup health confirmed: display, font, touch; rollback cancelled' in main_log
margin = int(re.search(r'stack margin=(\d+) bytes', main_log)[1])
assert margin >= 8192
assert 'Recovery HTTP ready; panel=1; SD writes disabled' in rescue_log
assert 'DHCP server started' in rescue_log
assert 'Hash of data verified.' in log('device-touch-main-write-0.2.2.log')
assert 'Hash of data verified.' in log('device-touch-rescue-write-0.2.2.log')
assert 'ThingsOS_touch: down' in touch_log and 'ThingsOS_touch: up' in touch_log
assert touch_log.count('ThingsOS_touch: tap') >= 1
for action in range(1, 5):
    assert f'ThingsOS_keys: action={action} page=1' in touch_log, 'Reading key not exercised'
assert 'Panel full clear before frame 0' in main_log
assert 'Panel full clear before frame 6' in log('device-touch-main-prekeys-0.2.2.log')
assert digest(build / 'native-idf/ThingsOS_native.elf')[:9] in touch_log
physical = json.loads((build / 'device-physical-touch-0.2.2.json').read_text(encoding='utf-8'))
assert physical['user_confirmed_touch'] and physical['user_confirmed_ghosting_cleared']
assert physical['user_confirmed_capacitive_keys']
selector = (build / 'device-healthy-ota-select-0.2.2.bin').read_bytes()
assert len(selector) == 8192
entries = [struct.unpack_from('<I20sII', selector, offset) for offset in (0, 4096)]
assert any(entry[0] == 4 and entry[2] == 2 and entry[3] ==
           (binascii.crc32(struct.pack('<I', 4), 0xffffffff) & 0xffffffff) for entry in entries)
assert any(entry[0] == 3 and entry[2] == 2 for entry in entries), 'Previous healthy fallback missing'
assert 'Defaulting to factory image' in log('device-main-0.2.1.log')
assert 'ThingsOS_rescue' in log('device-main-0.2.1.log')
report = dict(version='0.2.2', verified_at=datetime.now(timezone.utc).isoformat(),
              main_sha256=digest(main), rescue_sha256=digest(rescue),
              bootloader_sha256=digest(build / 'native-idf/bootloader/bootloader.bin'),
              main_health_confirmed=True, main_stack_margin_bytes=margin,
              main_ota_state='VALID', running_slot='ota_1', rescue_panel_confirmed=True,
              rescue_ap_http_server_started=True, failed_trial_returned_to_factory=True,
              raw_credentials_published=False, physical_touch_confirmed=True,
              physical_ghosting_cleared=True, previous_healthy_fallback_preserved=True,
              physical_capacitive_keys_confirmed=True,
              hardware_qualified=False, ota_power_loss_qualified=False)
(build / 'device-repair-validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
