# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
from zipfile import ZipFile,ZIP_DEFLATED
import struct,zlib

def generate(root):
    root=Path(root)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    # Deliberately small portrait source proves proportional upscaling, with a
    # border and internal detail making accidental cropping detectable.
    rows=[]
    for y in range(300):
        row=bytearray()
        for x in range(200):
            value=225
            if x<3 or x>=197 or y<3 or y>=297:value=30
            elif 20<x<180 and (30<y<43 or 54<y<60 or 250<y<255):value=55
            elif abs(x-100)+abs(y-155)<60:value=85
            row.append(value)
        rows.append(b'\0'+row)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',200,300,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
    with ZipFile(root/'reading-cover-local.epub','w',ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip')
        z.writestr('META-INF/container.xml','<container><rootfile full-path="OPS/book.opf"/></container>')
        z.writestr('OPS/book.opf','<package><metadata><dc:title xmlns:dc="x">Portrait Cover</dc:title></metadata><manifest><item id="c" href="cover.xhtml" media-type="application/xhtml+xml"/><item id="b" href="body.xhtml" media-type="application/xhtml+xml"/><item id="art" href="art.png" media-type="image/png" properties="cover-image"/></manifest><spine><itemref idref="c"/><itemref idref="b"/></spine></package>')
        z.writestr('OPS/cover.xhtml','<html><body><h1>Cover</h1><img src="art.png" alt="Cover"/></body></html>')
        z.writestr('OPS/body.xhtml','<html><body><h1>Chapter One</h1><p>BODY_START</p><img src="art.png" alt="Ordinary figure"/><p>BODY_END</p></body></html>')
        z.writestr('OPS/art.png',png)
if __name__=='__main__':
    import sys
    generate(sys.argv[1])
