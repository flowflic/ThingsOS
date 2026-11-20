from pathlib import Path
r=Path(__file__).resolve().parents[2]/'build/native-state-fixtures'
for name in ('.thingsos','books/目录','fonts','empty','WhiteOS','System Volume Information'):(r/name).mkdir(parents=True,exist_ok=True)
(r/'books/目录/故事.txt').write_text('Example book for state and directory tests',encoding='utf-8')
(r/'books/a.epub').write_bytes(b'Fixture listing only; not an EPUB parsing fixture')
(r/'.hidden.txt').write_text('hidden',encoding='utf-8')
print(r)

(r/'WhiteOS/legacy.txt').write_text('preserve legacy files',encoding='utf-8')
