"""Small synthetic EPUBs at the configured resource-count boundaries."""
from pathlib import Path
import sys
import zipfile

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
limit = 10240

def book(name, resources=1, chapters=1, entries=4, extra="", guide="", body="BODY_MARKER"):
    manifest = "".join(f'<item id="c{i}" href="body.xhtml" media-type="application/xhtml+xml"/>' for i in range(resources))
    spine = '<itemref idref="c0"/>' * chapters
    package = f'<package><manifest>{manifest}{extra}</manifest><spine>{spine}</spine>{guide}</package>'
    with zipfile.ZipFile(root / name, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip')
        archive.writestr('META-INF/container.xml', '<container><rootfile full-path="OPS/book.opf"/></container>')
        archive.writestr('OPS/book.opf', package)
        archive.writestr('OPS/body.xhtml', f'<html><body><h1>Chapter</h1><p>{body}</p></body></html>')
        for i in range(entries - 4):
            archive.writestr(f'OPS/unused/{i}', '')

book('zip-limit.epub', entries=limit)
book('zip-over.epub', entries=limit + 1)
book('manifest-limit.epub', resources=limit)
book('manifest-over.epub', resources=limit + 1)
book('spine-limit.epub', chapters=limit)
book('spine-over.epub', chapters=limit + 1)
book('duplicate-css.epub', extra='<item id="css" href="a.css" media-type="text/css"/><item id="css" href="b.css" media-type="text/css"/>')
book('duplicate-chapter.epub', extra='<item id="c0" href="other.xhtml" media-type="application/xhtml+xml"/>')
book('duplicate-mixed.epub', extra='<item id="c0" href="a.css" media-type="text/css"/>')
large_body = "contents " * 80000
book('large-toc.epub', guide='<guide><reference type="toc" href="body.xhtml"/></guide>', body=large_body)
book('large-chapter.epub', body=large_body)
print(root)
