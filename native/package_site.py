"""Stage the user-authorized experimental release; never flash hardware or mark it qualified."""
from pathlib import Path
from datetime import datetime, timezone
import binascii
import hashlib
import json
import shutil
import struct
import subprocess
import zipfile
from patch_web_flasher import patch as patch_flasher

NATIVE = Path(__file__).resolve().parent
ROOT = NATIVE.parent
WORKSPACE = ROOT.parent
BUILD = ROOT / 'build/native-idf'
SITE = ROOT / 'build/native-site-current'
LAYOUT = 'thingsos-native-single-v2'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def record(path):
    data = path.read_bytes()
    return dict(path=path.relative_to(SITE).as_posix(), size=len(data), sha256=digest(data))


def package():
    validation = json.loads((BUILD / 'validation.json').read_text(encoding='utf-8'))
    assert validation['build_dependency_boundary_passed'] and validation['boot_guard_linked']
    assert 'online_release_manifest' in validation['host_tests'] and '100% tests passed' in validation['host_tests']
    assert 'online_download_resume' in validation['host_tests']
    assert validation['device_keyboard_file_browser_bookmark_stats_connected']
    images = {}
    for item in validation['images']:
        data = Path(item['file']).read_bytes()
        assert len(data) == item['bytes'] and digest(data) == item['sha256'], 'Stale build validation'
        images[int(item['development_offset'], 0)] = data
    table = images[0x8000]
    partitions = {}
    for at in range(0, len(table)-31, 32):
        magic, kind, sub, offset, size, label, flags = struct.unpack_from('<HBBII16sI', table, at)
        if magic != 0x50aa:
            break
        name = label.split(b'\0')[0].decode()
        partitions[name] = dict(type=kind, subtype=sub, offset=offset, size=size, flags=flags)
    expected = {'rescue': (0, 0, 0x20000, 0x200000), 'ota_0': (0, 16, 0x220000, 0xae0000), 'system_font': (1, 64, 0xd00000, 0x300000),
                'otadata': (1, 0, 0xf000, 0x2000), 'nvs': (1, 2, 0x9000, 0x6000)}
    for name, values in expected.items():
        assert tuple(partitions[name][key] for key in ('type', 'subtype', 'offset', 'size')) == values
    assert set(partitions)=={"nvs","otadata","phy_init","rescue","ota_0","system_font"}
    main = images[0x220000]
    assert main[0] == 0xe9 and struct.unpack_from('<H', main, 12)[0] == 9
    assert main[80:112].split(b'\0')[0] == b'ThingsOS_native'
    assert images[0x20000][80:112].split(b'\0')[0] == b'ThingsOS_rescue'
    assert images[0][2:4] == bytes([2, 0x4f]), 'Expected DIO / 16MiB / 80MHz boot header'
    version = main[48:80].split(b'\0')[0].decode()
    assert version == '0.3.6'
    assert images[0x20000][48:80].split(b'\0')[0].decode() == version
    # One main slot; factory rescue performs staged installs. Version rollback disabled.
    initial = bytearray(b'\xff' * 0x2000)
    crc = binascii.crc32(struct.pack('<I', 1), 0xffffffff) & 0xffffffff
    initial[:32] = struct.pack('<I20sII', 1, b'\xff'*20, 0, crc)
    assert crc == 0x4743989a
    images[0xf000] = bytes(initial)
    regions = sorted(images.items())
    end = 0
    for offset, data in regions:
        assert offset >= end and offset + len(data) <= 0x1000000
        end = offset + len(data)
    merged = bytearray(b'\xff' * end)
    for offset, data in regions:
        merged[offset:offset+len(data)] = data
    # All unused bytes, including NVS and the inactive OTA slot, must be erased in a migration.
    assert merged[0x9000:0xf000] == b'\xff' * 0x6000
    assert merged[0x220000+len(main):0xd00000] == b'\xff' * (0xae0000-len(main))
    assert struct.unpack_from('<I', merged, 0xf000)[0] == 1
    assert struct.unpack_from('<I', merged, 0xf000+24)[0] == 0
    assert struct.unpack_from('<I', merged, 0xf000+28)[0] == crc
    for offset, data in regions:
        assert merged[offset:offset+len(data)] == data
    # This generated publish tree contains the current release only.
    assert SITE.resolve().parent == (ROOT / 'build').resolve()
    assert SITE.name == 'native-site-current' and not SITE.is_symlink()
    if SITE.exists():
        shutil.rmtree(SITE)
    SITE.mkdir(parents=True)
    # Read the optional font asset without publishing any historical firmware.
    old = ROOT / 'build/site'
    old_release = json.loads((old / 'release.json').read_text(encoding='utf-8'))
    assert old_release['version'] == '0.1.1'
    web_vendor = WORKSPACE / '.tools/web-deploy/node_modules/esp-web-tools'
    assert json.loads((web_vendor / 'package.json').read_text(encoding='utf-8'))['version'] == '10.4.0'
    shutil.copytree(web_vendor / 'dist/web', SITE / 'vendor', dirs_exist_ok=True)
    reset_adapter = patch_flasher(SITE / 'vendor')
    subprocess.run(['node', str(NATIVE / 'build_web_flasher.cjs'), str(SITE)], check=True)
    inline_engine = record(SITE / 'vendor/esptool.js')
    shutil.copyfile(NATIVE / 'site/index.html', SITE / 'index.html')
    shutil.copyfile(NATIVE / 'site/verify-release.js', SITE / 'verify-release.js')
    for name in ('styles.css', 'site.js', 'inline-installer.js', 'flash-engine.js', 'feedback.css', 'feedback.js'):
        shutil.copyfile(NATIVE / 'site' / name, SITE / name)
    shutil.copytree(NATIVE / 'site/images', SITE / 'images')
    shutil.copyfile(ROOT / 'web-flasher/_headers', SITE / '_headers')
    (SITE / '404.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>固件已下架</title><h1>此固件已下架</h1><p>本站仅提供当前版本。</p><a href="/">返回 ThingsOS 安装页</a></html>', encoding='utf-8')
    (SITE / 'firmware').mkdir(exist_ok=True)
    usb = SITE / f'firmware/ThingsOS-{version}-{digest(merged)[:12]}-usb.bin'
    ota = SITE / f'firmware/ThingsOS-{version}-{digest(main)[:12]}-ota.bin'
    usb.write_bytes(merged)
    ota.write_bytes(main)
    font = images[0xd00000]
    release = dict(schema=2, name='ThingsOS', version=version, project='ThingsOS_native', board='RDP-G01-W',
                   layout=LAYOUT, status='experimental-not-hardware-qualified', firmware_ready=False,
                   packaged_at=datetime.now(timezone.utc).isoformat(), usb=record(usb), ota=record(ota),
                   rescue=dict(offset='0x20000', size=len(images[0x20000]), sha256=digest(images[0x20000])),
                   system_font=dict(name='方正屏显雅宋', full_font=True, offset='0xd00000', size=len(font), sha256=digest(font)),
                   usb_required_for_incompatible_layout=True, ota_compatible_project='ThingsOS_native',
                   ota_requires_layout=LAYOUT, device_validation='not performed',
                   limitations=validation['limitations'])
    notes=json.loads((NATIVE/'release-notes.json').read_text(encoding='utf-8'))
    assert notes['version']==version
    release['previous_version']=notes['previous_version']
    release['changes']=notes['changes']
    release['prior_feature_validation']=dict(version='0.2.0',
        scope='Prior device-tested features; this release includes later changes and current-image hardware acceptance remains pending',
        accepted=['USB direct mode and device close switch','Keyboard typography, response and ghosting',
                  'Manual-only update checks without page interruption','LAN address and WiFi status',
                  'Image uploads, oriented dimensions and file preview return'])
    if validation.get('wifi_page_scan_async'):
        release['network_update']=dict(hotspot_ssid='ThingsOS',hotspot_password='picoread',
            wifi_scan_async=True,first_frame_before_scan=True,scan_cache_ms=30000)
    accepted_path = ROOT / 'build/device-reader-gc16-stable-20261017-report.json'
    if accepted_path.is_file():
        accepted = json.loads(accepted_path.read_text(encoding='utf-8-sig'))
        if accepted.get('boot_health') == 'passed' and accepted.get('user_feedback') == '左侧残影已消失，全刷正常':
            release['reader_logic_validation'] = dict(
                tested_version=accepted['version'], tested_application_sha256=accepted['sha256'],
                user_feedback=accepted['user_feedback'],
                ordinary_refresh_count=accepted['turn_log_summary']['ordinary_gc16_refresh_count'],
                direct_full_refresh_count=accepted['turn_log_summary']['direct_gc16_full_count'],
                scan_errors=accepted['turn_log_summary']['errors'],
                scope='Prior device-tested reader logic; this application image includes later changes and has not been flashed')
    release['parts'] = [dict(offset=hex(offset), size=len(data), sha256=digest(data)) for offset, data in regions]
    release['web_flasher_reset'] = reset_adapter
    release['web_flasher_inline_engine'] = inline_engine
    device_report = ROOT / 'build/device-repair-validation.json'
    if device_report.is_file():
        device = json.loads(device_report.read_text(encoding='utf-8'))
        if (device.get('main_sha256') == digest(main) and
                device.get('rescue_sha256') == digest(images[0x20000]) and
                device.get('bootloader_sha256') == digest(images[0]) and
                device.get('main_health_confirmed') and device.get('rescue_panel_confirmed')):
            release['device_validation'] = dict(scope='Read Pico basic startup recovery',
                main_first_frame=True, main_health_confirmed=True, rescue_panel=True,
                rescue_ap_http_server=True, physical_touch_confirmed=device.get('physical_touch_confirmed', False),
                physical_ghosting_cleared=device.get('physical_ghosting_cleared', False),
                physical_capacitive_keys_confirmed=device.get('physical_capacitive_keys_confirmed', False),
                complete_functional_acceptance=False, ota_power_loss_qualified=False)
    source = SITE / f'ThingsOS-{version}-{digest(main)[:12]}-source.zip'
    official = WORKSPACE / 'references/read-pico-official'
    with zipfile.ZipFile(source, 'w', zipfile.ZIP_DEFLATED) as archive:
        def tree(folder, prefix):
            for path in sorted(folder.rglob('*')):
                relative = path.relative_to(folder)
                if any(p in {'build', '__pycache__', '.git', '.netlify', 'node_modules', 'managed_components'} for p in relative.parts):
                    continue
                if path.is_file() and path.name != '.git' and path.suffix not in {'.bin', '.elf', '.map', '.log', '.pyc'} and path.name != 'sdkconfig.old':
                    archive.write(path, (Path(prefix) / relative).as_posix())
        tree(NATIVE, 'ThingsOS/native')
        for name in ('read_pico', 'epdiy', 'e0470_epaper_waveform', 'cst836u', 'fca9555', 'sy7636a', 'read_pico_pmu', 'sc7a20h', 'pwm_audio'):
            tree(official / 'components' / name, 'references/read-pico-official/components/' + name)
        for name in ('LICENSE', 'AGENTS.md', 'upstream-sources.json'):
            archive.write(ROOT / name, 'ThingsOS/' + name)
        archive.write(WORKSPACE / 'fonts/方正屏显雅宋.TTF', 'fonts/方正屏显雅宋.TTF')
        archive.write(ROOT / 'tools/audit_source_origins.py', 'ThingsOS/tools/audit_source_origins.py')
        archive.writestr('SOURCE-NOTES.txt', 'Independent ThingsOS native development snapshot. ESP-IDF v6.1 required. Official selected BSP commit: '+validation['official_bsp_commit']+'\nLegacy application sources excluded. Hardware acceptance pending.\n')
    release['source'] = record(source)
    if 'sd_fonts' in old_release:
        name = old_release['sd_fonts']['path']
        shutil.copyfile(old / name, SITE / name)
        release['sd_fonts'] = record(SITE / name)
        assert release['sd_fonts']['sha256'] == old_release['sd_fonts']['sha256']
    manifest = dict(name='ThingsOS · Read Pico · 独立工程开发测试版', version=version,
                    new_install_prompt_erase=True, new_install_improv_wait_time=0,
                    builds=[dict(chipFamily='ESP32-S3', parts=[dict(path=release['usb']['path'], offset=0)])])
    notices = ['ThingsOS independent native application: Apache-2.0.\n',
               'Book-cover lock-screen preview: Alice\'s Adventures in Wonderland, Project Gutenberg ebook 11, https://www.gutenberg.org/ebooks/11 . Source ebook metadata: Public domain in the USA. Screenshot rendered by ThingsOS from the EPUB cover.\n',
               'Selected official Read Pico BSP: '+validation['official_bsp_commit']+'\n',
               'FreeType 2.14.3 (FTL), zlib 1.3.1, stb_image (MIT), cJSON 1.7.19 (MIT), TinyUSB 0.21.0~2 (MIT), pypinyin 0.55.0 dictionary (MIT).\n',
               'Full default font supplied by the user; font distribution rights are not granted by code licenses.\n',
               'ESP Web Tools vendor reset dispatch modified to call the independent ThingsOS S3 RTC reset adapter; original bundle SHA256: '+reset_adapter['original_sha256']+'\n',
               'This distribution contains the current independent firmware only.\n']
    notices.append('\n--- ThingsOS Keyboard Sans (SIL OFL 1.1) ---\n'+
        (NATIVE/'firmware/main/assets/OFL-Keyboard-Sans.txt').read_text(encoding='utf-8'))
    notices.append('\nInline installer uses esptool-js 0.6.1 directly, with the independent ThingsOS S3 reset adapter; no post-install Improv reconnect.\n')
    for name in ('esp-web-tools', '@material/web', 'esptool-js', 'atob-lite', 'improv-wifi-serial-sdk', 'lit',
                 'tslib', 'pako', 'lit-html', 'lit-element', '@lit/reactive-element'):
        package_root = web_vendor.parent / name
        licenses = [p for p in package_root.glob('*') if p.is_file() and p.name.lower() in
                    {'license', 'license.txt', 'license.md', 'copying', 'copyrightnotice.txt'}]
        assert licenses, 'Missing web dependency license: ' + name
        for license_file in licenses:
            notices.append('\n--- Web dependency '+name+' ---\n'+license_file.read_text(encoding='utf-8',errors='replace'))
    for folder in (NATIVE / 'vendor', official / 'components/read_pico', official / 'components/epdiy',
                   official / 'components/e0470_epaper_waveform', official / 'components/cst836u',
                   official / 'components/fca9555', official / 'components/sy7636a',
                   official / 'components/read_pico_pmu', official / 'components/sc7a20h',
                   official / 'components/pwm_audio'):
        for path in sorted(folder.rglob('*')):
            if path.is_file() and path.name.lower() in {'license', 'license.txt', 'ftl.txt'}:
                notices.append('\n--- '+path.relative_to(WORKSPACE).as_posix()+' ---\n'+path.read_text(encoding='utf-8', errors='replace'))
    notices.append('\n--- Apache-2.0 ---\n'+(ROOT / 'LICENSE').read_text(encoding='utf-8'))
    (SITE / 'THIRD_PARTY_NOTICES.txt').write_text(''.join(notices), encoding='utf-8')
    for name, value in [('manifest.json', manifest), ('release.json', release)]:
        (SITE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    report = dict(version=version, site=str(SITE), part_hashes_verified=True, first_boot_ota_slot=0,
                  first_boot_state='NEW', usb_bytes=len(merged), application_bytes=len(main), hardware_qualified=False)
    (ROOT / 'build/native-package-validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    package()
