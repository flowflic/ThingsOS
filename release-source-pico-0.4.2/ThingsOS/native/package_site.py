"""Stage the user-authorized firmware release; hardware evidence is recorded separately."""
from pathlib import Path
from datetime import datetime, timezone
import binascii
import argparse
import hashlib
import html
import json
import shutil
import struct
import subprocess
import sys
from patch_web_flasher import patch as patch_flasher

NATIVE = Path(__file__).resolve().parent
ROOT = NATIVE.parent
WORKSPACE = ROOT.parent
BUILD = ROOT / 'build/native-idf'
SITE = ROOT / 'build/native-site-current'
LAYOUT = 'thingsos-native-single-v2'
# Metalio E-Ink4-Plus (ESP32-S31) is packaged from its own build tree and
# published on the same site as a second install target.
METALIO_USB = ROOT / 'build/native-usb'
METALIO_LAYOUT = 'thingsos-metalio-single-v1'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def record(path):
    data = path.read_bytes()
    return dict(path=path.relative_to(SITE).as_posix(), size=len(data), sha256=digest(data))


METALIO_NOTICE = (
    '\n--- ThingsOS Metalio (E-Ink4-Plus) install assets ---\n'
    'S31 installer uses esptool-js 0.7.0 (Apache-2.0) with the esptool 5.5.0 ESP32-S31 stub,\n'
    'which is GPL-2.0; the full stub license text ships as licenses/esptool-5.5.0-GPL-2.0.txt.\n'
    'The S31 reset adapter under vendor/metalio/thingsos-s31-reset.js is part of ThingsOS.\n'
)


def add_metalio_notice(site):
    """Append the Metalio license notice once, keeping the rest of the file intact."""
    path = site / 'THIRD_PARTY_NOTICES.txt'
    text = path.read_text(encoding='utf-8')
    if 'ThingsOS Metalio (E-Ink4-Plus) install assets' not in text:
        path.write_text(text + METALIO_NOTICE, encoding='utf-8')


def stage_metalio(site):
    """Stage the E-Ink4-Plus (ESP32-S31) install assets next to the Pico release.

    The Metalio images are packaged by native/package_metalio.py, which also
    guarantees the main and rescue descriptors share one version. This only
    copies verified artifacts and generates the release metadata the page reads.
    """
    manifest_path = METALIO_USB / 'metalio-manifest.json'
    assert manifest_path.is_file(), 'Missing packaged Metalio manifest; run native/package_metalio.py'
    packaged = json.loads(manifest_path.read_text(encoding='utf-8'))
    version = packaged['version']
    assert packaged['chip'] == 'esp32s31'
    usb_src = METALIO_USB / packaged['usb_image']['file']
    ota_src = METALIO_USB / packaged['ota_image']['file']
    assert usb_src.is_file() and ota_src.is_file(), 'Packaged Metalio images are missing'
    for path, entry in ((usb_src, packaged['usb_image']), (ota_src, packaged['ota_image'])):
        data = path.read_bytes()
        assert len(data) == entry['bytes'] and digest(data) == entry['sha256'], 'Stale Metalio package'
    # Second install target on the same page; the S31 installer and stub are
    # shipped from site/vendor/metalio, not from the build tree.
    shutil.copyfile(NATIVE / 'site/device-switch.js', site / 'device-switch.js')
    shutil.copyfile(NATIVE / 'site/verify-metalio-release.js', site / 'verify-metalio-release.js')
    shutil.copyfile(NATIVE / 'site/metalio-installer.js', site / 'metalio-installer.js')
    shutil.copyfile(NATIVE / 'site/metalio-flash-engine.js', site / 'metalio-flash-engine.js')
    shutil.copytree(NATIVE / 'site/vendor/metalio', site / 'vendor/metalio', dirs_exist_ok=True)
    shutil.copytree(NATIVE / 'site/licenses', site / 'licenses', dirs_exist_ok=True)
    (site / 'firmware/metalio').mkdir(parents=True, exist_ok=True)
    subprocess.run(['python', str(NATIVE / 'generate_metalio_release.py'), '--site', str(site)], check=True)
    staged = json.loads((site / 'metalio/release.json').read_text(encoding='utf-8'))
    assert staged['version'] == version and staged['layout'] == METALIO_LAYOUT
    assert staged['status'] == 'experimental-not-hardware-qualified' and staged['firmware_ready'] is False
    assert staged['usb']['sha256'] == packaged['usb_image']['sha256']
    assert staged['ota']['sha256'] == packaged['ota_image']['sha256']
    # Only the current version may be published.
    for path in (site / 'firmware/metalio').glob('*.bin'):
        assert version in path.name, 'Stale Metalio image left in the package: ' + path.name
    return staged


def preserve_published_metalio(site, baseline):
    """Carry the existing second-device release byte-for-byte into a Pico update."""
    baseline = Path(baseline).resolve()
    assert baseline.is_relative_to((ROOT / 'build').resolve()) and baseline != site.resolve()
    release = json.loads((baseline / 'metalio/release.json').read_text(encoding='utf-8'))
    assert release['board'] == 'Metalio-E-Ink4-Plus' and release['chip'].lower().replace('-', '') == 'esp32s31'
    assert release['layout'] == METALIO_LAYOUT and release['status'] == 'release'
    assert release['firmware_ready'] is True
    for kind in ('usb', 'ota'):
        entry = release[kind]
        raw = (baseline / entry['path']).read_bytes()
        assert len(raw) == entry['size'] and digest(raw) == entry['sha256']
    prefixes = ('metalio/', 'firmware/metalio/', 'vendor/metalio/', 'images/metalio/', 'licenses/')
    names = {'device-switch.js', 'verify-metalio-release.js', 'metalio-installer.js', 'metalio-flash-engine.js'}
    preserved = []
    for source in sorted(baseline.rglob('*')):
        if not source.is_file():
            continue
        relative = source.relative_to(baseline).as_posix()
        if relative not in names and not relative.startswith(prefixes):
            continue
        destination = site / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        assert destination.read_bytes() == source.read_bytes()
        preserved.append(record(destination))
    assert preserved and (site / 'metalio/release.json').is_file()
    release_version=json.loads((NATIVE/'release-notes.json').read_text(encoding='utf-8'))['version'].replace('.','')
    (ROOT / f'build/release-{release_version}-metalio-preservation.json').write_text(
        json.dumps(dict(version=release['version'], baseline=str(baseline), files=preserved), indent=2) + '\n', encoding='utf-8')
    return release


def write_history(history_path, index_path, current_version):
    """Render the release-history block into the page shell between its markers."""
    history = json.loads(Path(history_path).read_text(encoding='utf-8'))
    assert history['current_version'] == current_version, 'History current_version mismatch'
    assert len({item['version'] for item in history['releases']}) == len(history['releases'])
    blocks = []
    groups = []
    if history.get('metalio', {}).get('releases'):
        groups.append(('E-INK4 PLUS', history['metalio']['releases']))
    groups.append(('Read Pico', history['releases']))
    for device, entries in groups:
        blocks.append('<p class="history-device">' + html.escape(device) + '</p>')
        for item in entries:
            assert set(item) <= {'version', 'previous_version', 'date', 'changes'}
            if device == 'Read Pico':
                assert item['version'] != current_version, 'Current version must not appear as history'
            blocks.append('<details><summary><span>v' + html.escape(item['version']) + '</span><time datetime="' +
                          html.escape(item['date'], quote=True) + '">' + html.escape(item['date']) +
                          '</time></summary><div class="history-body"><p>相比 ' +
                          html.escape(item['previous_version']) + ' 的更新</p><ul>' +
                          ''.join('<li>' + html.escape(change) + '</li>' for change in item['changes']) +
                          '</ul></div></details>')
    page = Path(index_path).read_text(encoding='utf-8')
    start = '<!-- release-history:start -->'
    end = '<!-- release-history:end -->'
    assert page.count(start) == page.count(end) == 1, 'Release-history markers missing'
    a = page.index(start) + len(start)
    b = page.index(end, a)
    Path(index_path).write_text(page[:a] + ''.join(blocks) + page[b:], encoding='utf-8')
    return blocks


def package(reuse_pico=False, preserved_metalio=None):
    """Stage the site.

    reuse_pico keeps the already-published Read Pico package byte-identical and
    only refreshes the shared shell plus the Metalio (E-Ink4-Plus) target. Use
    it when the Pico build tree has drifted from its recorded validation and the
    Pico firmware is not part of this change.
    """
    if reuse_pico:
        return package_reusing_pico()
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
    assert version == '0.4.2'
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
    for name in ('styles.css', 'site.js', 'inline-installer.js', 'flash-engine.js'):
        shutil.copyfile(NATIVE / 'site' / name, SITE / name)
    shutil.copytree(NATIVE / 'site/images', SITE / 'images')
    metalio = preserve_published_metalio(SITE, preserved_metalio) if preserved_metalio else stage_metalio(SITE)
    shutil.copyfile(ROOT / 'web-flasher/_headers', SITE / '_headers')
    (SITE / '404.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>固件已下架</title><h1>此固件已下架</h1><p>本站仅提供当前版本。</p><a href="/">返回 ThingsOS 安装页</a></html>', encoding='utf-8')
    (SITE / 'firmware').mkdir(exist_ok=True)
    usb = SITE / f'firmware/ThingsOS-{version}-{digest(merged)[:12]}-usb.bin'
    ota = SITE / f'firmware/ThingsOS-{version}-{digest(main)[:12]}-ota.bin'
    usb.write_bytes(merged)
    ota.write_bytes(main)
    font = images[0xd00000]
    release = dict(schema=2, name='ThingsOS', version=version, project='ThingsOS_native', board='RDP-G01-W',
                   layout=LAYOUT, status='release', firmware_ready=True,
                   packaged_at=datetime.now(timezone.utc).isoformat(), usb=record(usb), ota=record(ota),
                   rescue=dict(offset='0x20000', size=len(images[0x20000]), sha256=digest(images[0x20000])),
                   system_font=dict(name='方正屏显雅宋', full_font=True, offset='0xd00000', size=len(font), sha256=digest(font)),
                   usb_required_for_incompatible_layout=True, ota_compatible_project='ThingsOS_native',
                   ota_requires_layout=LAYOUT, device_validation='not performed',
                   limitations=validation['limitations'])
    notes=json.loads((NATIVE/'release-notes.json').read_text(encoding='utf-8'))
    assert notes['version']==version
    release['previous_version']=notes['previous_version']
    release['comparison_version']=notes['comparison_version']
    release['changes']=notes['changes']
    history=json.loads((NATIVE/'release-history.json').read_text(encoding='utf-8'))
    assert history['current_version']==version and history['releases'][0]['version']==notes['previous_version']
    write_history(NATIVE/'release-history.json', SITE/'index.html', version)
    shutil.copyfile(NATIVE/'release-history.json',SITE/'release-history.json')
    release['release_history']='release-history.json'
    prior_reader_path=ROOT/'build/reader-cachefix-device-validation-20261025.json'
    if prior_reader_path.is_file():
        prior_reader=json.loads(prior_reader_path.read_text(encoding='utf-8'))
        if prior_reader.get('startup_health_confirmed') and prior_reader.get('reading_test',{}).get('underruns')==0:
            release['prior_reader_validation']=dict(
                tested_version=prior_reader['version'],tested_application_sha256=prior_reader['application_sha256'],
                gc16_refreshes=prior_reader['reading_test']['gc16_refreshes'],underruns=0,
                recovery_redraws=prior_reader['reading_test']['recovery_redraws'],
                user_feedback=prior_reader.get('user_feedback',[]),
                scope='Prior device-tested reader changes; rebuilt '+version+' image has not been flashed')
    release['cloud_sync_validation'] = 'Real-account cloud reads/writes pending verification'
    release['prior_feature_validation_036'] = dict(
        version='0.3.6', application_sha256='7354d99ed24ce04380e05d93f17c25749ff17f2d649bb43b92b68e98e68392ec',
        scope='User accepted the prior development build; the rebuilt '+version+' image has not been installed on the device',
        accepted=['Independent home/reader refresh settings', 'Fast native monochrome text',
                  'Physical left-key back navigation', 'Inline More controls and saved settings'])
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
    acceptance=json.loads((ROOT/'build/pico-local-epub-install-20261112/installation.json').read_text(encoding='utf-8'))
    tests=json.loads((ROOT/'build/local-epub-20261112/result.json').read_text(encoding='utf-8'))
    assert acceptance['startup_health_confirmed'] and acceptance['main_byte_for_byte_readback']
    assert tests['host_tests_passed']==43 and tests['original_book_images']==109
    release['prior_candidate_validation']=dict(tested_version=acceptance['version'],
        tested_application_sha256=acceptance['application_sha256'],startup_health_confirmed=True,
        host_tests_passed=43,original_book_images_tested=109,exact_release_image_flashed=False,
        complete_functional_acceptance=False,
        scope='The preceding 0.4.0 fix candidate passed USB readback and startup health; 0.4.1 is rebuilt. Physical EPUB image clarity and runtime stress remain pending.')
    charge_fix_path=ROOT/'build/pico-charge-status-20261112/result.json'
    if charge_fix_path.is_file():
        charge_fix=json.loads(charge_fix_path.read_text(encoding='utf-8'))
        if charge_fix['main']['sha256']==digest(main):
            assert charge_fix['rescue']['sha256']==digest(images[0x20000])
            assert charge_fix['verification']['host_tests_passed']==43
            release['charge_status_validation']=dict(host_tests_passed=43,
                application_sha256=digest(main),rescue_sha256=digest(images[0x20000]),
                visual_reference='Metalio E-Ink4-Plus approved battery and lightning',
                exact_release_image_flashed=False,device_charge_transitions='pending')
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
    # Public deployment is firmware-only; do not stage source or extra font archives.
    official = WORKSPACE / 'references/read-pico-official'
    release['distribution'] = 'firmware-only'
    manifest = dict(name='ThingsOS · Read Pico · 正式版', version=version,
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
    notices.append(METALIO_NOTICE)
    for relative in ('vendor/libwebp/COPYING','vendor/libwebp/PATENTS'):
        notices.append('\n--- '+relative+' ---\n'+(NATIVE/relative).read_text(encoding='utf-8'))
    stb_license=(NATIVE/'vendor/stb/stb_image.h').read_text(encoding='utf-8').split('ALTERNATIVE A - MIT License',1)[1].split('ALTERNATIVE B - Public Domain',1)[0]
    notices.append('\n--- stb_image MIT license ---\n'+stb_license)
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
    for relative in ('vendor/tjpgd/NOTICE', 'vendor/pinyin/LICENSE.phrases'):
        notices.append('\n--- '+relative+' ---\n'+(NATIVE / relative).read_text(encoding='utf-8'))
    notices.append('\n--- Apache-2.0 ---\n'+(ROOT / 'LICENSE').read_text(encoding='utf-8'))
    (SITE / 'THIRD_PARTY_NOTICES.txt').write_text(''.join(notices), encoding='utf-8')
    for name, value in [('manifest.json', manifest), ('release.json', release)]:
        (SITE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    report = dict(version=version, metalio_version=metalio['version'], metalio_preserved=bool(preserved_metalio), site=str(SITE), part_hashes_verified=True, first_boot_ota_slot=0,
                  first_boot_state='NEW', usb_bytes=len(merged), application_bytes=len(main), hardware_qualified=False)
    (ROOT / 'build/native-package-validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


def package_reusing_pico():
    """Rebuild the site with the published Read Pico release kept byte-identical.

    The Pico firmware, its metadata and the vendor installer bundles are carried
    over from the existing packaged site. Only the shared page shell and the
    Metalio (E-Ink4-Plus) target are regenerated, so this changes nothing on the
    Pico side while the second device becomes installable.
    """
    assert SITE.resolve().parent == (ROOT / 'build').resolve()
    assert SITE.name == 'native-site-current' and SITE.is_symlink() is False
    assert SITE.is_dir(), 'No packaged site to reuse; run a full package first'
    existing_release = json.loads((SITE / 'release.json').read_text(encoding='utf-8'))
    existing_manifest = json.loads((SITE / 'manifest.json').read_text(encoding='utf-8'))
    pico_version = existing_release['version']
    assert pico_version == json.loads((NATIVE/'release-notes.json').read_text(encoding='utf-8'))['version'], 'Unexpected published Pico version: ' + pico_version
    for key in ('usb', 'ota'):
        entry = existing_release[key]
        data = (SITE / entry['path']).read_bytes()
        assert len(data) == entry['size'] and digest(data) == entry['sha256'], 'Published Pico image changed'
    # Carry the published Pico package into a fresh tree, then rebuild the shell.
    staging = ROOT / 'build/native-site-staging'
    if staging.exists():
        shutil.rmtree(staging)
    shutil.copytree(SITE, staging)
    try:
        shutil.rmtree(SITE)
        SITE.mkdir(parents=True)
        for name in ('firmware', 'vendor', 'images', '404.html', 'flash-engine.js', 'inline-installer.js',
                     'manifest.json', 'release-history.json', 'release.json', 'site.js', 'styles.css',
                     'verify-release.js', 'THIRD_PARTY_NOTICES.txt', '_headers'):
            source = staging / name
            if source.is_dir():
                shutil.copytree(source, SITE / name)
            else:
                shutil.copyfile(source, SITE / name)
        # Refresh the shared page shell and the second install target.
        shutil.copyfile(NATIVE / 'site/index.html', SITE / 'index.html')
        shutil.copyfile(NATIVE / 'site/styles.css', SITE / 'styles.css')
        shutil.copyfile(NATIVE / 'site/inline-installer.js', SITE / 'inline-installer.js')
        shutil.copytree(NATIVE / 'site/images', SITE / 'images', dirs_exist_ok=True)
        metalio = stage_metalio(SITE)
        # The page shell carries the release-history markers; fill them from the
        # recorded history so the refreshed shell keeps the published log.
        write_history(NATIVE / 'release-history.json', SITE / 'index.html', pico_version)
        add_metalio_notice(SITE)
        report = dict(version=pico_version, metalio_version=metalio['version'], site=str(SITE),
                      reused_pico_package=True, pico_usb_sha256=existing_release['usb']['sha256'],
                      metalio_usb_sha256=metalio['usb']['sha256'], hardware_qualified=False)
        (ROOT / 'build/native-package-validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, ensure_ascii=False))
    finally:
        shutil.rmtree(staging, ignore_errors=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-pico', action='store_true')
    parser.add_argument('--preserve-metalio-from', type=Path)
    args = parser.parse_args()
    assert not (args.reuse_pico and args.preserve_metalio_from)
    package(args.reuse_pico, args.preserve_metalio_from)
