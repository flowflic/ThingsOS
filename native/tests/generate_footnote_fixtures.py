# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

def generate(root):
    root=Path(root)
    with ZipFile(root/'footnote-compat.epub','w',ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip')
        z.writestr('META-INF/container.xml','<container><rootfile full-path="OPS/book.opf"/></container>')
        z.writestr('OPS/book.opf','<package><manifest><item id="c" href="body.xhtml" media-type="application/xhtml+xml"/><item id="n" href="notes.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
        z.writestr('OPS/body.xhtml','''<html xmlns:epub="http://www.idpf.org/2007/ops"><body><h1>Compat</h1><p>Cross <a epub:type="noteref" href="notes.xhtml#fn%3A1"><sup>[1]</sup></a>; icon <a class="duokan-footnote" href="#local"><img src="icon.png" alt="2"/></a>; empty <a role="doc-noteref" href="#empty"/>.</p><div id="fn:1">WRONG_LOCAL_BODY</div><aside id="local"><p>LOCAL_NOTE_BODY</p></aside><aside id="empty">EMPTY_NOTE_BODY</aside></body></html>''')
        z.writestr('OPS/notes.xhtml','<html><body><p><a id="fn:1">1</a>CROSS_FILE_NOTE_BODY</p></body></html>')
        z.writestr('OPS/icon.png',(root/'picture.png').read_bytes())
if __name__=='__main__':
    import sys
    generate(sys.argv[1])
