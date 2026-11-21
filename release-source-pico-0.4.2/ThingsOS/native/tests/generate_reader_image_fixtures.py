"""Synthetic inputs for reader fragmentation and large-JPEG regressions."""
from pathlib import Path
import sys
from PIL import Image
root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
im=Image.new('RGBA',(1080,700),(100,150,200,128));im.putpixel((0,0),(0,0,0,0));im.save(root/'rgba.png')
im=Image.new('RGB',(3800,7400),(64,64,64));im.save(root/'world-map-size.jpg',quality=85)
# Q00=16 at quality 50 reconstructs a white block as IDCT Y=256 in fast mode.
# Saturation must keep it white rather than casting it to zero.
im=Image.new('RGB',(2666,3994),(255,255,255));im.save(root/'white-table.jpg',quality=50)
im=Image.new('RGB',(240,320),(80,130,180));im.save(root/'small.jpg',quality=85)
im=Image.new('P',(1920,1028));im.putpalette([0,0,0,255,255,255]+[0]*762)
im.putpixel((0,0),1);im.save(root/'palette.png',transparency=0,bits=8)
im=Image.new('L',(17,13),96);im.save(root/'gray.png',transparency=96)
im=Image.new('LA',(17,13),(80,128));im.save(root/'gray-alpha.png')
im=Image.new('RGB',(17,13),(100,150,200));im.save(root/'rgb.png')
im=Image.new('RGB',(1080,1440),(70,70,70));im.save(root/'cover.webp',quality=85)
im=Image.new('RGBA',(16,12),(0,0,0,128));im.putpixel((0,0),(0,0,0,0));im.save(root/'alpha.webp',lossless=True)
print('Reader image fixtures ready')

im=Image.new('RGB',(768,1024));px=im.load()
for y in range(im.height):
    for x in range(im.width):px[x,y]=((x*7+y*13)%256,(x*19+y*3)%256,(x^y)%256)
for subsampling,name in [(2,'420'),(1,'422'),(0,'444')]:
    im.save(root/f'progressive-{name}.jpg',quality=88,progressive=True,subsampling=subsampling)
im.convert('L').save(root/'progressive-gray.jpg',quality=88,progressive=True)
im.convert('CMYK').save(root/'progressive-cmyk.jpg',quality=88,progressive=True)

# Keep a large resource table resident while decoding a lossy WebP. Unlike
# isolated image tests this exercises aggregate parser/ZIP/decoder PSRAM.
import zipfile
for name,size,filter_pattern in [('alpha-complex',(1080,801),False),('alpha-filtered',(1200,1000),True)]:
    im=Image.new('RGBA',size)
    px=im.load()
    for y in range(size[1]):
        for x in range(size[0]):
            alpha=(x*17+y*7+(x*y%31))%256 if not filter_pattern else (255 if (x//37+y//23)%3 else (x+y)%256)
            px[x,y]=(45,90,135,alpha)
    im.save(root/f'{name}.webp',quality=85,method=6)
    with Image.open(root/f'{name}.webp') as reference:
        (root/f'{name}.alpha').write_bytes(reference.getchannel('A').tobytes())
with zipfile.ZipFile(root/'illustrated-large.epub','w',compression=zipfile.ZIP_DEFLATED) as z:
    z.writestr('mimetype','application/epub+zip')
    z.writestr('META-INF/container.xml','<container><rootfile full-path="OPS/book.opf"/></container>')
    manifest=''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(2500))
    spine=''.join(f'<itemref idref="c{i}"/>' for i in range(2500))
    z.writestr('OPS/book.opf',f'<package><manifest>{manifest}<item id="image" href="cover.webp" media-type="image/webp"/></manifest><spine>{spine}</spine></package>')
    for i in range(2500):z.writestr(f'OPS/c{i}.xhtml',f'<html><body><h1>Chapter {i}</h1><p>READABLE</p><img src="cover.webp"/></body></html>')
    z.writestr('OPS/cover.webp',(root/'cover.webp').read_bytes())
