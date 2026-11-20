"""Explicit USB migration to one main slot. Preserve NVS/SD; back up first.

Requires the matching host/device acceptance report and esptool/pyserial runtime.
No website publication. A layout migration is not an ordinary application OTA.
"""
from pathlib import Path
import argparse,binascii,hashlib,json,struct,subprocess,sys,time
import serial
from serial.tools import list_ports
from esptool.reset import HardReset

ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'build'
def sha(data):return hashlib.sha256(data).hexdigest()
def partitions(data):
    out={}
    for at in range(0,len(data)-31,32):
        magic,kind,sub,offset,size,name,flags=struct.unpack_from('<HBBII16sI',data,at)
        if magic!=0x50aa:break
        out[name.split(b'\0')[0].decode()]=(kind,sub,offset,size)
    return out
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-mac',required=True)
    parser.add_argument('--expected-elf',required=True)
    parser.add_argument('--backup',type=Path,required=True)
    args=parser.parse_args()
    ports=[p for p in list_ports.comports() if (p.serial_number or '').lower()==args.expected_mac.lower()]
    assert len(ports)==1,'Expected USB device not uniquely present'
    port=ports[0].device
    validation=json.loads((BUILD/'native-idf/validation.json').read_text(encoding='utf-8'))
    assert validation['single_application_slot'] and not validation['rollback_config_enabled']
    assert '100% tests passed' in validation['host_tests'] and 'single_slot_failure_paths' in validation['host_tests']
    images={}
    for item in validation['images']:
        data=Path(item['file']).read_bytes();assert len(data)==item['bytes'] and sha(data)==item['sha256']
        images[int(item['development_offset'],0)]=data
    assert set(images)=={0,0x8000,0x20000,0x220000,0xd00000}
    layout=partitions(images[0x8000])
    assert layout=={'nvs':(1,2,0x9000,0x6000),'otadata':(1,0,0xf000,0x2000),'phy_init':(1,1,0x11000,0x1000),
                    'rescue':(0,0,0x20000,0x200000),'ota_0':(0,16,0x220000,0xae0000),'system_font':(1,64,0xd00000,0x300000)}
    app=images[0x220000];assert app[80:112].split(b'\0')[0]==b'ThingsOS_native' and len(app)<=0xae0000
    report={'version':app[48:80].split(b'\0')[0].decode(),'elf':app[176:208].hex(),'sha256':sha(app),
            'application_bytes':len(app),'app_capacity':0xae0000,'layout':'thingsos-native-single-v2',
            'port':port,'mac':args.expected_mac,'published':False,'verified_regions':[],'boot_health':'pending'}
    report_path=BUILD/'device-single-slot-20261018-report.json'
    def save():report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    save()
    log_path=BUILD/'migrate-single-slot-20261018.log'
    with log_path.open('w',encoding='utf-8') as log:
        def run(parts):
            subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','--port',port,'--baud','921600','--after','no-reset']+parts,stdout=log,stderr=subprocess.STDOUT,check=True)
            log.flush()
        if not args.backup.exists():run(['read-flash','0','0x1000000',str(args.backup)])
        backup=args.backup.read_bytes();assert len(backup)==0x1000000,'Full backup required'
        report['backup']=str(args.backup.resolve());report['backup_sha256']=sha(backup)
        args.backup.with_suffix(args.backup.suffix+'.sha256').write_text(sha(backup)+'\n',encoding='ascii')
        old=partitions(backup[0x8000:0x9000]);assert old['ota_0'][2:]==(0x220000,0x400000) and old['ota_1'][2:]==(0x620000,0x400000)
        entries=[]
        for at in (0xf000,0x10000):
            seq,_,state,crc=struct.unpack_from('<I20sII',backup,at)
            if 0<seq<0xfffffffd and state in (0,1,2) and crc==binascii.crc32(struct.pack('<I',seq),0xffffffff)&0xffffffff:entries.append((seq,state))
        assert entries;seq,state=max(entries);assert state==2
        active=0x220000+((seq-1)%2)*0x400000
        assert backup[active+176:active+208].hex()==args.expected_elf
        # On this migration the new main is entirely in the former inactive
        # slot. No running app executes during any USB/ROM write.
        assert active==0x620000 and 0x220000+len(app)<=active
        live=BUILD/'single-slot-before-live.bin';run(['read-flash','0','0x20000',str(live)])
        assert live.read_bytes()==backup[:0x20000],'Boot/NVS/selector changed since backup'
        header=BUILD/'single-slot-before-active.bin';run(['read-flash',hex(active),'0x200',str(header)])
        assert header.read_bytes()==backup[active:active+0x200]
        assert args.expected_mac.lower() in log_path.read_text(encoding='utf-8').lower()
        report.update(before_elf=args.expected_elf,before_partition='ota_1',nvs_preserved=False,sd_untouched=True);save()
        stage=BUILD/('single-slot-candidate-'+sha(app)[:12]);stage.mkdir(exist_ok=True)
        def write_verified(offset,data):
            source=stage/f'{offset:08x}.bin';source.write_bytes(data)
            run(['write-flash','--flash-mode','dio','--flash-freq','80m','--flash-size','16MB',hex(offset),str(source)])
            target=stage/f'{offset:08x}-readback.bin';run(['read-flash',hex(offset),hex(len(data)),str(target)])
            assert sha(target.read_bytes())==sha(data),f'Readback mismatch at {offset:x}'
            report['verified_regions'].append({'offset':hex(offset),'bytes':len(data),'sha256':sha(data)});save()
            print(f'Verified USB region {offset:#x}: {len(data)} bytes',flush=True)
        # Prepare and verify applications/font before switching layout.
        for offset in (0x220000,0xd00000,0x20000,0):write_verified(offset,images[offset])
        selector=bytearray(b'\xff'*8192)
        struct.pack_into('<I20sII',selector,0,1,b'\xff'*20,0,binascii.crc32(struct.pack('<I',1),0xffffffff)&0xffffffff)
        write_verified(0xf000,bytes(selector))
        write_verified(0x8000,images[0x8000])
        nvs=BUILD/'single-slot-nvs-readback.bin';run(['read-flash','0x9000','0x6000',str(nvs)])
        assert nvs.read_bytes()==backup[0x9000:0xf000]
        report['nvs_preserved']=True;save()
    p=serial.Serial(None,115200,timeout=.2);p.dtr=False;p.rts=False;p.port=port;p.open()
    boot=bytearray();print('Migration verified; rebooting for startup health',flush=True)
    with (BUILD/'boot-single-slot-20261018.log').open('wb',buffering=0) as log:
        HardReset(p,uses_usb=True)();end=time.monotonic()+65
        while time.monotonic()<end:
            try:chunk=p.read(8192)
            except (serial.SerialException,OSError):
                p.close();time.sleep(.3)
                try:p.open()
                except (serial.SerialException,OSError):pass
                continue
            if chunk:log.write(chunk);boot.extend(chunk)
            if b'Startup health confirmed: display, font, touch; single app' in boot:break
    p.close();text=boot.decode('utf-8',errors='replace')
    assert 'Loaded app from partition at offset 0x220000' in text
    assert 'ELF file SHA256:  '+report['elf'][:9] in text
    assert 'Startup health confirmed: display, font, touch; single app' in text
    assert 'Init static tx buffer num: 16' in text and 'Init static rx buffer num: 16' in text
    report['boot_health']='passed over USB';save();print(json.dumps(report,indent=2),flush=True)
if __name__=='__main__':main()
