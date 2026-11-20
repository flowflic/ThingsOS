# SPDX-License-Identifier: Apache-2.0
"""Generate a bounded offline lexicon from pinned MIT word/pronunciation data."""
from pathlib import Path
import hashlib,json,re,sys,unicodedata
root=Path(__file__).resolve().parent
single=json.loads((root/'vendor/pinyin/pinyin_dict.json').read_text(encoding='utf-8'))
phrases=json.loads((root/'vendor/pinyin/phrases_dict.json').read_text(encoding='utf-8'))
def normalize(s):
    s=unicodedata.normalize('NFD',s).replace('u\u0308','v')
    return ''.join(c for c in s if not unicodedata.combining(c))
def spell(word):
    result=[];at=0
    while at<len(word):
        for end in range(len(word),at+1,-1):
            part=word[at:end]
            if part in phrases:
                result.extend(normalize(c[0]) for c in phrases[part]);at=end;break
        else:
            pron=single.get(str(ord(word[at])),'').split(',')[0]
            if not pron:return ''
            result.append(normalize(pron));at+=1
    return ''.join(result)
words={}
for line in (root/'vendor/jieba/dict.txt').read_text(encoding='utf-8').splitlines():
    word,freq,*_=line.split();freq=int(freq)
    if 2<=len(word)<=8 and freq>=10 and all('\u4e00'<=c<='\u9fff' for c in word):words[word]=freq
for word in '书库 微读 读书 阅读 划线 想法 笔记 书架 目录 缓存 翻页 横屏 字体 粗细 灵敏度 重启 喜欢 你好 谢谢 再见 重新 设置 输入 拼音 中文 测试 发现 感觉 精彩 内容 作者 故事 学习 生活 世界 因为 所以 但是 可以 觉得 知道 需要 应该 我们 他们 自己 确认 同步'.split():
    words[word]=max(words.get(word,0),1000)
entries=[]
for word,freq in sorted(words.items(),key=lambda x:(-x[1],x[0]))[:60000]:
    key=spell(word)
    if re.fullmatch('[a-zv]{2,48}',key):entries.append((key,word,freq))
entries.sort(key=lambda x:(x[0],-x[2],x[1]))
assert len(entries)<65536
lines=['/* Generated from pinned MIT pypinyin + jieba data. See vendor/sources.json. */',
 'typedef struct {const char* key;const char* word;unsigned frequency;} ime_word_t;',
 'static const ime_word_t ime_words[]={']
for key,word,freq in entries:lines.append('{'+json.dumps(key)+','+json.dumps(word,ensure_ascii=False)+','+str(freq)+'},')
lines+=['};','static const unsigned short ime_context_index[]={']
index=sorted(range(len(entries)),key=lambda i:entries[i][1])
for at in range(0,len(index),32):lines.append(','.join(map(str,index[at:at+32]))+',')
lines+=['};','']
text='\n'.join(lines);output=root/'src/ime_words.inc'
if '--check' in sys.argv:assert output.read_text(encoding='utf-8')==text
else:output.write_text(text,encoding='utf-8')
print(json.dumps({'words':len(entries),'generated_bytes':len(text.encode()),'sha256':hashlib.sha256(text.encode()).hexdigest()}))
