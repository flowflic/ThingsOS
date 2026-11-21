# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

def generate(root):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    titles=['3 Chapter','3.1 Section','3.1.1 Detail','3.2 Next section']
    anchors=['s3','s31','s311','s32']
    body='<html><body>'+''.join(f'<h3 id="{a}">{t}</h3><p>ONLY_{i}</p>' for i,(a,t) in enumerate(zip(anchors,titles)))+'</body></html>'
    links=[f'<a href="body.xhtml#{a}">{t}</a>' for a,t in zip(anchors,titles)]
    nav='<html><body><nav epub:type="toc"><ol><li>'+links[0]+'<ol><li>'+links[1]+'<ol><li>'+links[2]+'</li></ol></li><li>'+links[3]+'</li></ol></li></ol></nav></body></html>'
    def point(i):return f'<navPoint id="n{i}"><navLabel><text>{titles[i]}</text></navLabel><content src="body.xhtml#{anchors[i]}"/>'
    ncx='<ncx><navMap>'+point(0)+point(1)+point(2)+'</navPoint></navPoint>'+point(3)+'</navPoint></navPoint></navMap></ncx>'
    flat='<ncx><navMap>'+''.join(point(i)+'</navPoint>' for i in range(4))+'</navMap></ncx>'
    for name,toc,ext,media,prop in [('hierarchy-nav',nav,'xhtml','application/xhtml+xml',' properties="nav"'),('hierarchy-ncx',ncx,'ncx','application/x-dtbncx+xml',''),('hierarchy-legacy',flat,'ncx','application/x-dtbncx+xml','')]:
        with ZipFile(root/(name+'.epub'),'w',ZIP_DEFLATED) as z:
            z.writestr('mimetype','application/epub+zip')
            z.writestr('META-INF/container.xml','<container><rootfile full-path="OPS/book.opf"/></container>')
            z.writestr('OPS/book.opf',f'<package><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/><item id="nav" href="toc.{ext}" media-type="{media}"{prop}/></manifest><spine toc="nav"><itemref idref="body"/></spine></package>')
            z.writestr('OPS/body.xhtml',body);z.writestr('OPS/toc.'+ext,toc)
if __name__=='__main__':
    import sys
    generate(sys.argv[1])
