from pathlib import Path
import zipfile,shutil,struct
from PIL import Image,ImageDraw
workspace=Path(__file__).resolve().parents[3]
r=workspace/'ThingsOS/build/native-fixtures';r.mkdir(parents=True,exist_ok=True)
shutil.copyfile(workspace/'fonts/方正屏显雅宋.TTF',r/'default.ttf')
image=Image.new('RGBA',(240,180),'white');draw=ImageDraw.Draw(image);draw.rectangle((30,30,210,150),fill=(80,80,80,255));image.save(r/'picture.png')
image.convert('RGB').save(r/'picture.jpg')
transparent=Image.new('RGBA',(1,1),(0,0,0,0));transparent.save(r/'transparent.png')
def book(path,hostile=False):
 with zipfile.ZipFile(path,'w') as z:
  z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
  z.writestr('META-INF/container.xml','<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>',compress_type=zipfile.ZIP_DEFLATED)
  opf='<package><metadata><dc:title xmlns:dc="x">山野来信 &amp; 测试</dc:title></metadata><manifest><item id="c1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/><item id="cover" href="images/cover.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="c2"/><itemref idref="c1"/></spine></package>'
  if hostile:opf='<!DOCTYPE x [<!ENTITY bad SYSTEM "file:///etc/passwd">]>'+opf
  z.writestr('OPS/book.opf',opf,compress_type=zipfile.ZIP_DEFLATED)
  z.writestr('OPS/text/ch1.xhtml','<!DOCTYPE html><html><body><h1>第一章</h1><p>桥边有风，山间有光。</p></body></html>',compress_type=zipfile.ZIP_DEFLATED)
  z.writestr('OPS/text/ch2.xhtml','<html><head><style>hidden</style></head><body><h1>第二章</h1><p>溪水穿过山谷。</p><script>secret_script</script><img src="../images/cover.png" alt="图一：旧桥"/><p>过桥之后，继续向前。&#x4e2d;</p></body></html>',compress_type=zipfile.ZIP_DEFLATED)
  z.writestr('OPS/images/cover.png',(r/'picture.png').read_bytes(),compress_type=zipfile.ZIP_DEFLATED)
book(r/'valid.epub');book(r/'entity.epub',True)
raw=bytearray((r/'valid.epub').read_bytes());start=raw.find(b'PK\x01\x02');raw[start+16]^=1;(r/'crc.epub').write_bytes(raw)
(r/'truncated.epub').write_bytes(raw[:24])
(r/'damaged.ttf').write_bytes(b'not a font'*10)
print(r)

# EPUB3: nested caption markup, landmarks and multiple logical chapters per file.
with zipfile.ZipFile(r/'navigation.epub','w') as z:
 z.writestr('mimetype','application/epub+zip')
 z.writestr('META-INF/container.xml','<container><rootfile full-path="OPS/book.opf"/></container>')
 z.writestr('OPS/book.opf','<package><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine><itemref idref="body"/></spine></package>')
 z.writestr('OPS/nav.xhtml','<html><body><nav epub:type="toc"><ol><li><a href="body.xhtml#a"><span>第一章</span> &amp; <em>起点</em></a></li><li><a href="body.xhtml#b">第二章 终点</a></li></ol></nav><nav epub:type="landmarks"><a href="body.xhtml">正文</a></nav></body></html>')
 z.writestr('OPS/body.xhtml','<html><body><h3 id="a">第一章</h3><p>ONLY_FIRST</p><h3 id="b">第二章</h3><p>ONLY_SECOND</p></body></html>')

# Cover decoder must retain PNG transparency while using one channel for opaque images.
opaque=Image.new('RGB',(1200,1600),(64,64,64));opaque.save(r/'opaque-cover.jpg');opaque.save(r/'opaque-cover.png')
palette=Image.new('P',(2,1));palette.putpalette([0,0,0,0,0,0]+[0]*762);palette.putdata([0,1]);palette.save(r/'palette-alpha.png',transparency=0)

# Footnotes: in-document link markers plus the note bodies they point at. One
# note is long enough to need a second window page; extraction also has to strip
# markup and squash whitespace.
long_note='注解长文：'+'这一段用来验证脚注窗口内的翻页行为，文字需要足够长以便占据多屏。'*12+'（长文结束）'
with zipfile.ZipFile(r/'footnote.epub','w') as z:
 z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
 z.writestr('META-INF/container.xml','<container><rootfile full-path="OPS/book.opf"/></container>',compress_type=zipfile.ZIP_DEFLATED)
 z.writestr('OPS/book.opf','<package><metadata><dc:title xmlns:dc="x">脚注测试</dc:title></metadata><manifest><item id="c1" href="text/fn.xhtml" media-type="application/xhtml+xml"/><item id="c2" href="text/plain.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c1"/><itemref idref="c2"/></spine></package>',compress_type=zipfile.ZIP_DEFLATED)
 z.writestr('OPS/text/fn.xhtml','<html><body><h1>第一章</h1><p>正文第一句<a href="#fn1" id="ref1" epub:type="noteref">1</a>继续正文，又是<a href="#fn2" id="ref2" epub:type="noteref">2</a>第二处引用。</p><p>第二段文字，没有脚注。</p><aside id="fn1" epub:type="footnote">\n  <p>注解：这是脚注正文 <em>强调</em> 结束。</p>\n</aside><aside id="fn2" epub:type="footnote">\n  <p>'+long_note+'</p>\n</aside></body></html>',compress_type=zipfile.ZIP_DEFLATED)
 z.writestr('OPS/text/plain.xhtml','<html><body><h1>第二章</h1><p>本章没有任何脚注引用。</p></body></html>',compress_type=zipfile.ZIP_DEFLATED)


# Native-size alternating pixels distinguish original cover detail from a thumbnail.
def lock_cover_fixture():
 import zlib
 def chunk(t,data):return struct.pack('>I',len(data))+t+data+struct.pack('>I',zlib.crc32(t+data)&0xffffffff)
 row=b'\0'+bytes([0,255])*342
 png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',684,869,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(row*869))+chunk(b'IEND',b'')
 with zipfile.ZipFile(r/'valid.epub') as src,zipfile.ZipFile(r/'lock-cover.epub','w') as dst:
  for entry in src.infolist():
   dst.writestr(entry,png if entry.filename=='OPS/images/cover.png' else src.read(entry.filename))
lock_cover_fixture()

from generate_toc_fixtures import generate as generate_toc
generate_toc(r)

from generate_footnote_fixtures import generate as generate_footnotes
generate_footnotes(r)

from generate_reading_cover_fixtures import generate as generate_reading_covers
generate_reading_covers(r)

from generate_epub_compat_fixtures import generate as generate_epub_compat
generate_epub_compat(r)
