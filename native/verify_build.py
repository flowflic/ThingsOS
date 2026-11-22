"""Validate development build inputs and artifacts; no hardware or originality certificate."""
from pathlib import Path
import hashlib, json, os, subprocess, re
native = Path(__file__).resolve().parent
version = (native / 'VERSION').read_text(encoding='ascii').strip()
# Shared toolchain layout, same default as build.ps1. The build links this tree's own
# reference checkout; provenance comes from the pinned git checkout beside the tools.
tools = Path(os.environ.get('THINGSOS_TOOLS') or r'D:\crossmux\.tools')
workspace = tools.parent
build = native.parent / 'build/native-idf'
rescue = native.parent / 'build/native-rescue'
official = native.parent / 'references/read-pico-official'
if not official.is_dir(): official = workspace / 'references/read-pico-official'
official_git = official
if not (official_git / '.git').exists() and (workspace / 'references/read-pico-official/.git').exists():
    official_git = workspace / 'references/read-pico-official'
expected = json.loads((native.parent / 'upstream-sources.json').read_text(encoding='utf-8'))['official']['commit']
def run(args):
    return subprocess.run([str(a) for a in args],check=True,capture_output=True,text=True,encoding='utf-8',errors='replace').stdout.strip()
assert run(['git','-C',official_git,'rev-parse','HEAD']) == expected
assert not run(['git','-C',official_git,'status','--porcelain']), 'Official BSP checkout changed'
if official != official_git:
    def tree_digest(root):
        return {str(q.relative_to(root)): hashlib.sha256(q.read_bytes()).hexdigest()
                for q in root.rglob('*') if q.is_file() and '.git' not in q.parts}
    assert tree_digest(official) == tree_digest(official_git), 'Reference copy differs from the pinned checkout'
run([workspace / '.tools/host-python/Scripts/python.exe', native / 'prepare_dependencies.py'])
run([workspace / '.tools/host-python/Scripts/python.exe', native / 'generate_ime.py','--check'])
keyboard_assets=native/'firmware/main/assets'
keyboard_meta=json.loads((keyboard_assets/'keyboard-sans.json').read_text(encoding='utf-8'))
assert hashlib.sha256((keyboard_assets/'keyboard-sans.ttf').read_bytes()).hexdigest()==keyboard_meta['sha256']
assert hashlib.sha256((official/'sdcard/fonts/ChillDuanSansVF.ttf').read_bytes()).hexdigest()==keyboard_meta['source_sha256']
assert (keyboard_assets/'OFL-Keyboard-Sans.txt').read_bytes()==(official/'sdcard/fonts/OFL.txt').read_bytes()
assert 'app.ripple_enabled=false;' in (native/'firmware/main/main.c').read_text(encoding='utf-8')
assert '.bcdDevice=0x0306' in (native/'firmware/components/thingsos_board/usb_storage.c').read_text(encoding='utf-8')
test_output = run([workspace / '.tools/idf-tools/tools/cmake/4.0.3/bin/ctest.exe','--test-dir',native.parent / 'build/native-host-cmake','--output-on-failure'])
required_tests = {'runtime','ota_failure_paths','single_slot_failure_paths','font_psram_pool','online_download_resume','network_service_lifecycle','keyboard_files_persistence','browser_http_sha256',
    'reader_image_ui','epub_font_images_guard','device_ui_controller','display_pack_thumbnail_cache','weidu_workflow','weidu_notes_progress_time',
    'stagger_waveform_failure_paths','usb_disk_ownership_io','online_release_manifest','browser_image_prepare'}
assert '100% tests passed' in test_output and required_tests <= set(re.findall(r'Test\s+#\d+: (\S+)',test_output))
# Target frame evidence plus a reproducible patch of the pinned original.
from patch_freetype_pool import generate as generate_font_pool
pool_expected=generate_font_pool(native/'vendor/freetype/src/smooth',build/'verify-freetype-pool')
for directory in (build,rescue):
    patched=directory/'esp-idf/thingsos_libs/freetype-pool'
    for name,digest in pool_expected['generated'].items():
        assert hashlib.sha256((patched/name).read_bytes()).hexdigest()==digest, 'Stale FreeType pool patch'
font_options=(native/'vendor/freetype/include/freetype/config/ftoption.h').read_text()
raster_pool=int(re.search(r'^#define FT_RENDER_POOL_SIZE\s+(\d+)L',font_options,re.M)[1])
prefetch_source=(native/'firmware/main/reader_cache.inc').read_text()
stack_extra=int(re.search(r'READER_PREP_STACK_BYTES \(FT_RENDER_POOL_SIZE \+ (\d+)u \* 1024u\)',prefetch_source)[1])*1024
prefetch_budget=raster_pool+stack_extra
raster_stack_frames=[]
for directory in (build,rescue):
    usage=directory/'esp-idf/thingsos_libs/freetype/CMakeFiles/freetype.dir/__/freetype-pool/smooth.c.su'
    frame=int(re.search(r':gray_convert_glyph\t(\d+)\t',usage.read_text())[1])
    assert frame<1024, 'Gray workspace still occupies the target stack'
    assert frame+8192<=prefetch_budget, 'Raster caller headroom exceeded'
    raster_stack_frames.append(frame)
names = ['read_pico','epdiy','e0470_epaper_waveform','cst836u','fca9555','sy7636a','read_pico_pmu','sc7a20h','pwm_audio']
allowed = [native.resolve(), (workspace / '.tools/esp-idf').resolve()] + [(official / 'components' / n).resolve() for n in names]
inventory = []
for directory in (build, rescue, build / 'bootloader'):
    commands = json.loads((directory / 'compile_commands.json').read_text(encoding='utf-8'))
    generated_dirs = [d for d in (directory / 'waveform-workspace', directory / 'reader-tls', directory / 'esp-idf/thingsos_libs/freetype-pool') if d.is_dir()]
    generated = {q.resolve() for d in generated_dirs for q in d.rglob('*') if q.is_file() and q.suffix in ('.c', '.S')}
    # Embedded assets, the ELF stub and other build-root sources are generated by the build.
    generated |= {q.resolve() for q in directory.iterdir() if q.is_file() and q.suffix in ('.c', '.S')}
    sources = []
    for command in commands:
        path = Path(command['file'])
        if not path.is_absolute(): path = Path(command['directory']) / path
        path = path.resolve()
        assert path in generated or any(path.is_relative_to(p) for p in allowed), path
        assert path.is_file(), path
        sources.append({'file':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    inventory.append({'build':str(directory),'sources':sources})
for application in inventory[:2]:
    paths=[s['file'].replace('\\','/') for s in application['sources']]
    assert sum(p.endswith('/thingsos_board/scan_adapter.c') for p in paths)==1
    assert not any(p.endswith('/output_lcd/render_lcd.c') for p in paths), 'Duplicate LCD renderer'
assert any('/thingsos_boot_guard/hooks.c' in s['file'].replace('\\','/') for s in inventory[2]['sources'])
for project in ('firmware','rescue'):
    config = (native / project / 'sdkconfig').read_text(encoding='utf-8')
    for item in ('# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set','CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC=y','CONFIG_IDF_TARGET="esp32s31"','CONFIG_CACHE_L1_DCACHE_LINE_SIZE=64'):
        assert item in config, item
    if project == 'firmware':
        assert 'CONFIG_SPIRAM_IGNORE_NOTFOUND=y' not in config, 'Main firmware requires PSRAM for network and TLS'
        assert 'CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y' in config
        assert 'CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y' in config
        assert 'CONFIG_ESP_MAIN_TASK_STACK_SIZE=20480' in config
        assert 'CONFIG_COMPILER_STACK_CHECK_MODE_STRONG=y' in config
        assert 'CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y' in config, 'TLS must not compete for fragmented internal RAM'
        assert 'CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y' not in config
        wifi_static = int(re.search(r'^CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=(\d+)$', config, re.M)[1])
        wifi_dynamic = int(re.search(r'^CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=(\d+)$', config, re.M)[1])
        wifi_window = int(re.search(r'^CONFIG_ESP_WIFI_RX_BA_WIN=(\d+)$', config, re.M)[1])
        assert wifi_static >= wifi_window, 'WiFi RX buffers must cover the AMPDU receive window'
        assert wifi_dynamic == 0 or wifi_dynamic >= wifi_static
    else:
        assert 'CONFIG_SPIRAM_IGNORE_NOTFOUND=y' in config, 'Rescue retains no-PSRAM recovery'
images = []
for image, offset, capacity, project in [
    (build/'ThingsOS_native.bin',0x220000,0xae0000,'ThingsOS_native'),
    (rescue/'ThingsOS_rescue.bin',0x20000,0x200000,'ThingsOS_rescue'),
    (build/'system_font.bin',0xd00000,0x300000,None),
    (build/'bootloader/bootloader.bin',0x2000,0x8000,None),
    (build/'partition_table/partition-table.bin',0x8000,0x1000,None)]:
    data = image.read_bytes(); assert 0 < len(data) <= capacity
    if project:
        assert data[0] == 0xe9
        assert data[80:112].split(b'\0',1)[0].decode() == project
        assert data[48:80].split(b'\0',1)[0].decode() == version, 'Firmware descriptor must match native/VERSION'
    images.append({'file':str(image),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'development_offset':hex(offset),'capacity':capacity})
font = (build/'system_font.bin').read_bytes(); original=(workspace/'fonts/方正屏显雅宋.TTF').read_bytes()
root_pem = (native/'firmware/components/thingsos_weidu/certs/weread_root.pem').read_text(encoding='ascii').strip()
trusted_store = (workspace/'.tools/esp-idf/components/mbedtls/esp_crt_bundle/cacrt_all.pem').read_text(encoding='utf-8')
assert root_pem in trusted_store, 'WeRead CA must come from the trusted SDK root store'
root_pem_bytes = (native/'firmware/components/thingsos_weidu/certs/weread_root.pem').read_bytes().strip()
assert root_pem_bytes in (build/'ThingsOS_native.bin').read_bytes(), 'Full WeRead CA not embedded in application'
assert font[:8]==b'THFONT01' and font[48:]==original and font[12:44]==hashlib.sha256(original).digest()
assert (build/'partition_table/partition-table.bin').read_bytes()==(rescue/'partition_table/partition-table.bin').read_bytes()
assert json.loads((build/'flasher_args.json').read_text())['app']['offset']=='0x220000'
assert 'thingsos_device_acceptance_required' not in (build/'build.ninja').read_text(encoding='utf-8')
assert 'usb-bundle' in (build/'build.ninja').read_text(encoding='utf-8')
assert 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' in (native/'firmware/sdkconfig').read_text()
usb_args=json.loads((build/'flasher_args.json').read_text())
assert set(usb_args['flash_files'])=={'0x2000','0x8000','0xf000','0x20000','0x220000','0xd00000'}
assert usb_args['otadata']['file']=='ota_data_usb.bin'
selector=(build/'ota_data_usb.bin').read_bytes()
import struct
assert len(selector)==8192 and struct.unpack_from('<I',selector)[0]==1
assert struct.unpack_from('<II',selector,24)==(0,0x4743989a)
assert selector[32:]==b'\xff'*(8192-32)
decoder=(native/'src/image_decode.c').read_text()
assert 'heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)' in decoder
assert 'heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)' in decoder
stagger=(native/'firmware/components/thingsos_board/stagger.inc').read_text()
assert 'enum EpdDrawMode drive_mode=MODE_GC16' in stagger, 'Stagger must drive GC16'
assert 'MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT' in stagger, 'Stagger LUT working set must stay in internal RAM'
assert 'epd_width()!=1216 || epd_height()!=684' in stagger, 'Stagger is bound to the 1216x684 panel'
assert 'thingsos_panel_reader_ripple_wave(' in stagger
assert 'reader_cache_before_scan' in prefetch_source
reader_header=(native/'include/thingsos/reader_ui.h').read_text()
assert 'THINGSOS_READER_SIZE_DEFAULT = 46' in reader_header
network=(native/'firmware/components/thingsos_net/network.c').read_text(encoding='utf-8')
network_defaults=(native/'include/thingsos/network_defaults.h').read_text(encoding='utf-8')
assert '#define THINGSOS_AP_SSID "ThingsOS"' in network_defaults
assert '#define THINGSOS_AP_PASSWORD "picoread"' in network_defaults
assert 'esp_wifi_scan_start(&config,false)' in network
assert 'esp_wifi_scan_start(&config, true)' not in network
assert 'ap_ssid[]=THINGSOS_AP_SSID, ap_password[]=THINGSOS_AP_PASSWORD' in network
network_ui=(native/'firmware/main/network_ui.inc').read_text(encoding='utf-8')
assert 'wifi_scan_pending && !app.wifi_scan_running && !app.dirty' in network_ui
report = {'scope':'Independent module integration development build','version':version,'firmware_ready':False,
    'wifi_page_scan_async':True,'wifi_scan_after_first_frame':True,'wifi_scan_cache_ms':30000,
    'ap_ssid':'ThingsOS','ap_password_bytes':8,
    'font_gray_pool_psram_bytes':raster_pool,'main_stack_bytes':20480,'prefetch_stack_budget_bytes':prefetch_budget,'target_raster_stack_frames':raster_stack_frames,
    'image_decode_psram_only':True,'wallpaper_decoder_live_budget_bytes':3*1024*1024,
    'wallpaper_compressed_input_limit_bytes':1024*1024,'usb_default_flash_complete':True,
    'reader_default_px':46,'reader_stagger_full_pixels':True,'reader_full_refresh_stagger_gc16':False,
    'reader_full_refresh_direct_global_gc16':True,'reader_prefetch_joins_before_dma':True,
    'reader_text_transition_gc16':True,'reader_stagger_internal_working_lut_bytes':8192,
    'build_dependency_boundary_passed':True,'official_bsp_commit':expected,'compiled_sources':sum(len(v['sources']) for v in inventory),
    'host_tests':test_output,'images':images,'application_bytes':images[0]['bytes'],'application_sha256':images[0]['sha256'],
    'rescue_and_font_images_present':True,'rollback_config_enabled':False,'single_application_slot':True,'boot_guard_linked':True,
    'device_keyboard_file_browser_bookmark_stats_connected':True,'font_complete':True,'usb_flash_enabled':True,'device_validation':'not performed','published':False,
    'book_management_connected':True,'pinyin_candidates_connected':True,'book_date_stats_connected':True,
    'upload_collision_cancel_connected':True,'reader_close_before_transfer':True,
    'limitations':['Complete device functional acceptance pending; basic startup evidence is recorded separately','Single main slot; physical power-loss recovery not qualified',
        'EPUB renderer uses bounded native heading/paragraph/image layout, not arbitrary CSS',
        'EPUB navigation and rendered logical chapters are bounded to 512 entries'],
    'compiled_input_inventory':'compiled-inputs.json'}
(build/'compiled-inputs.json').write_text(json.dumps(inventory,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(build/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
