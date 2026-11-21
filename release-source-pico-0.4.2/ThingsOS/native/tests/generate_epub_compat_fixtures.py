"""Synthetic EPUBs for MIME compatibility and independent manifest/spine bounds."""
from pathlib import Path
import sys
import zipfile


def generate(root):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)

    def book(name, items=1, chapters=None, mime=b'application/epub+zip'):
        chapters = items if chapters is None else chapters
        manifest = ''.join(
            f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>'
            for i in range(items))
        spine = ''.join(f'<itemref idref="c{i % items}"/>' for i in range(chapters))
        with zipfile.ZipFile(root / name, 'w') as z:
            z.writestr('mimetype', mime, compress_type=zipfile.ZIP_STORED)
            z.writestr('META-INF/container.xml',
                '<container><rootfile full-path="OPS/book.opf"/></container>')
            z.writestr('OPS/book.opf',
                '<package><metadata><title>Compatibility fixture</title></metadata>'
                f'<manifest>{manifest}</manifest><spine>{spine}</spine></package>',
                compress_type=zipfile.ZIP_DEFLATED)
            for i in range(items):
                z.writestr(f'OPS/c{i}.xhtml',
                    f'<html><body><h1>Chapter {i}</h1><p>EPUB_COMPAT_{i:04d}</p></body></html>',
                    compress_type=zipfile.ZIP_DEFLATED)

    for name, mime in (
        ('mime-exact.epub', b'application/epub+zip'),
        ('mime-lf.epub', b'application/epub+zip\n'),
        ('mime-crlf.epub', b'application/epub+zip\r\n'),
        ('mime-spaces.epub', b'application/epub+zip \r\n '),
        ('mime-wrong.epub', b'application/zip'),
        ('mime-suffix.epub', b'application/epub+zipX'),
        ('mime-leading.epub', b' application/epub+zip'),
        ('mime-nul.epub', b'application/epub+zip\0'),
        ('mime-tab.epub', b'application/epub+zip\t'),
        ('mime-empty.epub', b''),
        ('mime-oversize.epub', b'application/epub+zip' + b' ' * 45),
    ):
        book(name, mime=mime)
    book('compilation-576-572.epub', items=576, chapters=572)
    book('compilation-576-572-crlf.epub', items=576, chapters=572,
         mime=b'application/epub+zip\r\n')
    book('capacity-512.epub', items=512)
    book('capacity-1024.epub', items=1024)
    book('capacity-manifest-over.epub', items=10241, chapters=1)
    book('capacity-spine-over.epub', items=1, chapters=10241)


if __name__ == '__main__':
    generate(sys.argv[1])
