"""Convert actual host-rendered firmware canvases to lossless website assets."""
from pathlib import Path
import hashlib,json,sys
from PIL import Image
native=Path(__file__).resolve().parents[1]
features_only='--features-only' in sys.argv
source=native.parent/('build/site-feature-screens' if features_only else 'build/site-screens');out=native/'site/images';out.mkdir(exist_ok=True)
names=('weread-highlights','weread-thoughts','footnote-card','weread-reading-time') if features_only else ('reading','reading-menu','library','statistics','wallpapers','lock-book-cover','weread-shelf','weread-offline')
records=[]
for name in names:
    im=Image.open(source/(name+'.pgm'))
    assert im.size==(684,1216)
    target=out/(name+'.webp');im.save(target,format='WEBP',lossless=True,method=6)
    records.append(dict(file=target.name,width=im.width,height=im.height,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
manifest=out/'provenance.json'
existing=json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else {}
existing.setdefault('source','Current firmware UI renderer via native/tools/render_site_screens.c')
existing.setdefault('content','Illustrative books and self-authored reading text; sample statistics; not physical device photographs')
by_name={item['file']:item for item in existing.get('images',[])}
for item in records:
    if features_only:item.update(firmware_version='0.3.5',illustrative_data=True,renderer='native/tools/render_site_screens.c --features-only')
    by_name[item['file']]=item
existing['images']=list(by_name.values())
manifest.write_text(json.dumps(existing,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Prepared',len(records),'lossless firmware UI previews; existing provenance retained.')
