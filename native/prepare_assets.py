"""Full original default font and a bitmap rescue fallback from that same font."""
from pathlib import Path
import hashlib,struct,sys
native=Path(__file__).resolve().parent
workspace=native.parent
font_path=workspace/'fonts/方正屏显雅宋.TTF'
font=None
if '--rescue-bitmap' in sys.argv:
 from PIL import Image,ImageDraw,ImageFont
 font=ImageFont.truetype(str(font_path),26)
lines=[]
for cp in range(32,127):
 if font is None:break
 im=Image.new('1',(18,32),1);ImageDraw.Draw(im).text((0,0),chr(cp),font=font,fill=0)
 bits=[]
 for y in range(32):
  for byte in range(3):
   v=0
   for k in range(8):
    x=byte*8+k
    if x<18 and im.getpixel((x,y))==0:v|=1<<(7-k)
   bits.append(v)
 lines.append('{'+','.join(str(v) for v in bits)+'}')
if font is not None:
 (native/'firmware/components/thingsos_board/rescue_ascii.h').write_text('// Generated from user default font for independent rescue fallback.\nstatic const unsigned char thingsos_rescue_ascii[95][96]={\n'+',\n'.join(lines)+'\n};\n',encoding='utf-8')
raw=font_path.read_bytes();blob=b'THFONT01'+struct.pack('<I',len(raw))+hashlib.sha256(raw).digest()+b'\0'*4+raw
build=native.parent/'build/native-idf';build.mkdir(exist_ok=True)
(build/'system_font.bin').write_bytes(blob)
print({'font_image_bytes':len(blob),'default_font_bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()})
