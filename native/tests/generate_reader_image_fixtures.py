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
