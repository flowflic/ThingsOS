# SPDX-License-Identifier: Apache-2.0
"""Reproduce the OFL keyboard subset using fontTools 4.56.0."""
from pathlib import Path
import hashlib,re,json
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools import subset
native=Path(__file__).resolve().parent;root=native.parents[1];source=root/'references/read-pico-official/sdcard/fonts/ChillDuanSansVF.ttf'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='966286fc44907d4123528e1efb98c08e51fff013cd083140fdad1bf6b593555c'
font=TTFont(source);print('axes',[(a.axisTag,a.defaultValue) for a in font['fvar'].axes],flush=True)
font=instantiateVariableFont(font,{a.axisTag:(400 if a.axisTag=='wght' else a.defaultValue) for a in font['fvar'].axes},inplace=True)
chars=set(chr(i) for i in range(32,127))
chars.update(re.findall('[\u4e00-\u9fff]',(native/'src/ime_table.inc').read_text(encoding='utf-8')))
chars.update('空格更多搜索连接下一步中英拼音字节，')
opts=subset.Options();opts.name_IDs=['*'];opts.name_legacy=True;opts.name_languages=['*'];opts.recalc_bounds=True
ss=subset.Subsetter(options=opts);ss.populate(text=''.join(sorted(chars)));ss.subset(font)
for record in font['name'].names:
 if record.nameID in (1,3,4,6,16,17):
  value={1:'ThingsOS Keyboard Sans',3:'ThingsOS-Keyboard-Sans-Regular',4:'ThingsOS Keyboard Sans Regular',6:'ThingsOSKeyboardSans-Regular',16:'ThingsOS Keyboard Sans',17:'Regular'}[record.nameID]
  record.string=value.encode(record.getEncoding())
out=native/'firmware/main/assets/keyboard-sans.ttf';font.save(out)
print(json.dumps({'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'subset_bytes':out.stat().st_size,'chars':len(chars),'subset_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}),flush=True)
missing=set(map(ord,chars))-set(font.getBestCmap());print('missing',len(missing))
