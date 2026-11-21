# SPDX-License-Identifier: Apache-2.0
"""Regenerate the independent single-syllable Pinyin table from pinned MIT data."""
from pathlib import Path
import json,unicodedata,re,sys
native=Path(__file__).resolve().parent
data=json.loads((native/'vendor/pinyin/pinyin_dict.json').read_text(encoding='utf-8'))
groups={}
for cp,pron in data.items():
 c=chr(int(cp))
 try:c.encode('gb2312')
 except UnicodeEncodeError:continue
 if not '\u4e00'<=c<='\u9fff':continue
 for py in pron.split(','):
  decomposed=unicodedata.normalize('NFD',py).replace('u\u0308','v')
  py=''.join(x for x in decomposed if not unicodedata.combining(x))
  if re.fullmatch('[a-zv]{1,6}',py):groups.setdefault(py,[]).append(c)
common='的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经之进着等部度家电力里如水化高自二理起小物现都实加量两体制机当使点从业本去性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想通并提直题党程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农指几九区强放决西被干做必战先回则任取据处理世车价保书读删名网字输设置删除重命名中文阅读书架文件目录小说测试'
lines=['/* Generated independently from pypinyin 0.55.0 MIT data; see vendor/pinyin. */','typedef struct {const char* syllable; const char* characters;} ime_entry_t;','static const ime_entry_t ime_table[]={']
for py,chars in sorted(groups.items()):
 chars=list(dict.fromkeys(chars));chars.sort(key=lambda c:(common.index(c) if c in common else len(common)+ord(c)))
 lines.append('{'+json.dumps(py)+','+json.dumps(''.join(chars),ensure_ascii=False)+'},')
lines.append('};')
generated='\n'.join(lines)+'\n'
if '--check' in sys.argv:
 assert (native/'src/ime_table.inc').read_text(encoding='utf-8')==generated, 'Pinyin table differs from pinned source data'
else:
 (native/'src/ime_table.inc').write_text(generated,encoding='utf-8')
print('Generated',len(groups),'syllables')
