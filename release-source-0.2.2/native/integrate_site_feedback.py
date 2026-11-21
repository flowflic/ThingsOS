"""Add the user-approved survey entry without regenerating firmware metadata."""
from pathlib import Path
import argparse
import shutil

SOURCE = Path(__file__).resolve().parent / 'site'


def integrate(site):
    site = Path(site).resolve()
    page = site / 'index.html'
    html = page.read_text(encoding='utf-8')
    fragment = (SOURCE / 'feedback-section.html').read_text(encoding='utf-8').strip()
    if 'id="feedback"' not in html:
        marker = '<section id="release-history"'
        if marker not in html:
            marker = '<section class="faq '
        assert html.count(marker) == 1, 'Expected one history/FAQ insertion point'
        html = html.replace(marker, fragment + '\n' + marker, 1)
    else:
        assert fragment in html, 'Existing feedback section differs; review before replacing'
    if 'class="feedback-nav"' not in html:
        marker = '<a class="nav-install"'
        assert html.count(marker) == 1
        html = html.replace(marker, '<a class="feedback-nav" href="#feedback">使用反馈</a>' + marker, 1)
    if 'href="feedback.css"' not in html:
        html = html.replace('</head>', '<link rel="stylesheet" href="feedback.css"></head>', 1)
    if 'src="feedback.js"' not in html:
        html = html.replace('</body>', '<script src="feedback.js" defer></script></body>', 1)
    for name in ('feedback.css', 'feedback.js'):
        if (SOURCE / name).resolve() != (site / name).resolve():
            shutil.copyfile(SOURCE / name, site / name)
    page.write_bytes(html.encode('utf-8'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('site', help='Existing complete static site directory')
    integrate(parser.parse_args().site)
