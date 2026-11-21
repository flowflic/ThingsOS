"""Bounded serial capture. Raw logs stay local; displayed credentials are redacted."""
import argparse
import re
import time
from pathlib import Path
import serial

p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
p.add_argument('--seconds',type=int,default=12)
p.add_argument('--reset',action='store_true')
p.add_argument('--start',action='store_true',help='Official S3 ROM connection, clear forced download and capture without closing port')
p.add_argument('--watchdog',action='store_true',help='Use official S3 watchdog reset after clearing download flag')
p.add_argument('--output',required=True)
p.add_argument('--stream',action='store_true',help='Print complete redacted log lines as they arrive')
p.add_argument('--stop-after-actions',type=int,default=0,help='Finish after this many completed ThingsOS UI actions (0 disables)')
p.add_argument('--minimum-seconds',type=int,default=0,help='Minimum capture time before the action stop condition')
a=p.parse_args()
connection=serial.Serial(port=None,baudrate=115200,timeout=0.2)
connection.dtr=False
connection.rts=False
connection.port=a.port
connection.open()
if a.reset:
    from esptool.reset import HardReset
    HardReset(connection,uses_usb=True)()
if a.start:
    from esptool import detect_chip
    chip=detect_chip(connection,connect_mode='default-reset')
    print(f'Strap=0x{chip.read_reg(chip.GPIO_STRAP_REG):08x}; forced-download=0x{chip.read_reg(chip.RTC_CNTL_OPTION1_REG):08x}')
    if a.watchdog:
        chip.write_reg(chip.RTC_CNTL_OPTION1_REG,0,chip.RTC_CNTL_FORCE_DOWNLOAD_BOOT_MASK)
        try:
            chip.watchdog_reset()
        except serial.SerialException:
            # USB disconnect during the reset is expected on Windows.
            connection.close()
            for attempt in range(100):
                try:
                    connection.open()
                    break
                except serial.SerialException:
                    time.sleep(0.1)
            else:
                raise
    else:
        chip.hard_reset()
    connection.timeout=0.2
chunks=[]
actions=0
until=time.monotonic()+min(max(a.seconds,1),900)
earliest=time.monotonic()+max(0,min(a.minimum_seconds,900))
pending=b''
try:
    # Flush raw data as it arrives so a later interruption cannot lose evidence.
    with Path(a.output).open('wb') as capture:
        while time.monotonic()<until:
            data=connection.read(max(1,min(connection.in_waiting,16384)))
            if data:
                chunks.append(data)
                capture.write(data)
                capture.flush()
            if data and (a.stream or a.stop_after_actions>0):
                pending+=data
                while b'\n' in pending:
                    line,pending=pending.split(b'\n',1)
                    line=re.sub(r'(?i)(password|token)=\S+',r'\1=[redacted]',line.decode('utf-8',errors='replace'))
                    if a.stream:
                        print(line,flush=True)
                    if 'ThingsOS_perf: Action id=' in line:
                        actions+=1
            if a.stop_after_actions>0 and actions>=a.stop_after_actions and time.monotonic()>=earliest:
                break
except KeyboardInterrupt:
    pass
finally:
    connection.close()
raw=b''.join(chunks)
Path(a.output).write_bytes(raw)
text=raw.decode('utf-8',errors='replace')
text=re.sub(r'(?i)(password|token)=\S+',r'\1=[redacted]',text)
if not a.stream:
    print(text or 'No serial output during capture; port opened successfully.')
print(f'Captured {len(raw)} bytes to {a.output}')
