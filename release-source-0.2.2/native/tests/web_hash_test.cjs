const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict'), crypto = require('node:crypto');
const source = fs.readFileSync(__dirname+'/../firmware/components/thingsos_net/web_sha256.js','utf8');
const context = vm.createContext({Uint8Array,Uint32Array,DataView,Math}); vm.runInContext(source,context);
for (const n of [0,1,3,55,56,63,64,65,4096,4*1024*1024]) {
 const bytes = Buffer.alloc(n); for(let i=0;i<n;i++) bytes[i]=(i*31+i%7)&255;
 assert.equal(context.thingsos_sha256(bytes),crypto.createHash('sha256').update(bytes).digest('hex'));
}
assert.equal(context.thingsos_sha256(new TextEncoder().encode('abc')),'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
console.log('HTTP SHA-256 fallback: 11 vectors including 4 MiB passed');
const header=Buffer.alloc(288);header[0]=0xe9;header.writeUInt16LE(0x20,12);header.writeUInt32LE(0xabcd5432,32);header.write('0.3.0',48);header.write('ThingsOS_native',80);
assert.equal(context.thingsos_firmware_header(header,1000000).version,'0.3.0');
for(const mutate of [b=>b[0]=0,b=>b.writeUInt16LE(5,12),b=>b[32]=0,b=>b[80]=0]) {
 const bad=Buffer.from(header);mutate(bad);assert.throws(()=>context.thingsos_firmware_header(bad,1000000));
}
assert.equal(context.thingsos_firmware_header(header,0xae0000).project,"ThingsOS_native");
assert.throws(()=>context.thingsos_firmware_header(header,0xae0001));
assert.throws(()=>context.thingsos_firmware_header(header.subarray(0,200),1000000));
console.log('OTA preflight: valid image and six mismatch/capacity cases passed');
header.fill(0x5a,176,208);
assert.equal(context.thingsos_firmware_header(header,1000000).elf,'5a'.repeat(32));

// Execute the actual browser controller with deterministic DOM/HTTP adapters.
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{id,value:id==='kind'?'books':'',hidden:false,disabled:false,dataset:{},classList:{toggle(){}},append(){},prepend(){},replaceChildren(){},setAttribute(){},querySelectorAll(){return []},removeAttribute(key){delete this[key]},showModal(){this.open=true},close(){this.open=false}});return elements.get(id)}
const choices=['cancel','both','replace'].map(value=>({dataset:{collision:value}}));
element('collision').querySelectorAll=()=>choices;
let lastXHR,requests=[],rescueUnauthorized=false;
let lastManageCount=1,listEntries=[],healthResponses=[],previewResponse={text:"安全正文 <script>文字</script>",title:"Chapter",chapter:0,chapters:2,offset:0,next:0};
class FakeXHR {
 constructor(){lastXHR=this;this.upload={};this.headers={}}
 open(method,path){this.path=path;this.method=method}
 setRequestHeader(k,v){this.headers[k]=v}
 send(file){this.file=file}
 abort(){this.onabort()}
}
const browser=vm.createContext({console,Set,URL,URLSearchParams,location:{search:''},thingsos_prepare_image:async file=>({file,info:"prepared image"}),XMLHttpRequest:FakeXHR,document:{getElementById:element,querySelectorAll:()=>[],createElement:()=>element('new-'+Math.random())},window:{addEventListener(){}},setTimeout:fn=>{fn();return 0},confirm:()=>true,prompt:()=> '中文重命名.txt',fetch:async(path,options)=>{
 requests.push({path,options});let value;
 if(rescueUnauthorized)return {ok:false,status:401,text:async()=> 'Enter rescue token'};
 if(path==='/api/manage'){lastManageCount=JSON.parse(options.body).names.length;value={job:5};}
 else if(path==='/api/status'){value=healthResponses.length?healthResponses.shift():{job:5,job_pending:false,job_done:lastManageCount,job_failed:0,elf:'5a'.repeat(32),healthy:true};if(value==='offline')throw Error('network unavailable');}
 else if(path.startsWith('/api/preview'))value=previewResponse;
 else value=listEntries;
 return {ok:true,headers:{get(name){const u=new URL(path,'http://device');if(name==='X-ThingsOS-Directory')return encodeURIComponent(u.searchParams.get('directory')||('/sdcard/'+(u.searchParams.get('kind')||'books')));if(name==='X-ThingsOS-Card')return '000000000000005b';return null}},blob:async()=>new Blob(['image']),json:async()=>value,text:async()=> 'OK'};
}});
const html=fs.readFileSync(__dirname+'/../firmware/components/thingsos_net/web.html','utf8');
const controller=html.split('<script src="/sha256.js"></script><script>')[1].split('</script>')[0];
vm.runInContext(controller,browser);
(async()=>{
 await element('connect').onclick();
 assert.equal(element('auth').hidden,true);
 assert(requests.some(r=>r.path==='/api/list?kind=books'));
 assert(requests.every(r=>!('X-ThingsOS-Token' in r.options.headers)));
 let sending=vm.runInContext('send("/api/upload", {size:10})',browser);lastXHR.abort();await assert.rejects(sending,/取消上传/);
 sending=vm.runInContext('send("/api/upload", {size:10}, {"X-ThingsOS-Collision":"both"})',browser);assert.equal(lastXHR.headers['X-ThingsOS-Token'],undefined);assert.equal(lastXHR.headers['X-ThingsOS-Collision'],'both');lastXHR.status=200;lastXHR.responseText='uploaded';lastXHR.onload();assert.equal(await sending,'uploaded');
 for(const choice of choices){const waiting=vm.runInContext('chooseCollision()',browser);choice.onclick();assert.equal(await waiting,choice.dataset.collision);assert.equal(element('collision').open,false)}
 vm.runInContext('selected=new Set(["old.txt"]);state={library:"/sdcard/books",card:"000000000000005b"};',browser);await vm.runInContext('manage(true)',browser);
 const operation=requests.find(r=>r.path==='/api/manage');assert.deepEqual(JSON.parse(operation.options.body),{names:['old.txt'],directory:'/sdcard/books',card:'000000000000005b',rename:'中文重命名.txt'});assert.equal(operation.options.headers['X-ThingsOS-Confirm'],'MANAGE');assert.match(element('result').textContent,/完成 1 项/);
 sending=vm.runInContext('send("/api/ota", {size:10})',browser);lastXHR.upload.onprogress({lengthComputable:true,loaded:10,total:10});assert.match(element('result').textContent,/等待设备校验/);lastXHR.status=200;lastXHR.responseText='staged';lastXHR.onload();await sending;
 healthResponses=['offline',{version:'0.3.6',elf:'00'.repeat(32),healthy:true},{version:'0.3.6',elf:'5a'.repeat(32),healthy:false},{version:'0.3.6',elf:'5a'.repeat(32),healthy:true}];
 vm.runInContext('pendingHealth="'+'5a'.repeat(32)+'";pendingBefore="'+'00'.repeat(32)+'";pendingHealthAt=Date.now()-120000',browser);await vm.runInContext('healthCheck()',browser);
 assert.equal(healthResponses.length,0);assert.equal(vm.runInContext('pendingHealth',browser),null);assert.match(element('result').textContent,/健康检查通过/);
 vm.runInContext('pendingHealth="'+'5a'.repeat(32)+'";pendingBefore=pendingHealth',browser);await vm.runInContext('healthCheck()',browser);assert.match(element('result').textContent,/无法仅凭指纹确认/);assert.doesNotMatch(element('result').textContent,/升级完成/);
 let fakeNow=0;browser.Date={now:()=>{fakeNow+=61000;return fakeNow}};healthResponses=Array(5).fill('offline');
 vm.runInContext('pendingHealth="'+'5a'.repeat(32)+'";pendingBefore="'+'00'.repeat(32)+'"',browser);await vm.runInContext('healthCheck()',browser);
 assert.equal(vm.runInContext('pendingHealth',browser),'5a'.repeat(32));assert.match(element('result').textContent,/暂时无法确认/);browser.Date=Date;vm.runInContext('clearHealth()',browser);
 healthResponses=[{version:'0.3.6',rescue:true,sd:false}];await element('connect').onclick();assert.match(element('ota-flow').textContent,/直接安装/);assert.doesNotMatch(element('ota-consent').textContent,/SD/);
 healthResponses=[{version:'0.3.6',rescue:false,sd:true,library:'/sdcard/books'}];await element('connect').onclick();assert.match(element('ota-flow').textContent,/SD 卡/);
 browser.otaFile={size:10};element('consent').checked=true;
 vm.runInContext('prepared={file:otaFile,digest:"ab".repeat(32),elf:"5a".repeat(32)}',browser);
 healthResponses=[{version:'0.3.6',rescue:false,elf:'00'.repeat(32),healthy:true},'offline',{version:'0.3.6',elf:'5a'.repeat(32),healthy:true}];
 const installing=element('install').onclick();await new Promise(setImmediate);assert.equal(lastXHR.path,'/api/ota');assert.equal(lastXHR.headers['X-ThingsOS-SHA256'],'ab'.repeat(32));lastXHR.status=200;lastXHR.responseText='staged';lastXHR.onload();await installing;
 assert.match(element('result').textContent,/升级完成/);assert.equal(vm.runInContext('pendingHealth',browser),null);
 element('consent').checked=true;vm.runInContext('prepared={file:otaFile,digest:"ab".repeat(32),elf:"5a".repeat(32)}',browser);healthResponses=[{version:'0.3.6',rescue:false,elf:'00'.repeat(32),healthy:true}];
 const failedInstall=element('install').onclick();await new Promise(setImmediate);lastXHR.onerror();await failedInstall;assert.match(element('result').textContent,/连接中断/);assert.equal(vm.runInContext('pendingHealth',browser),null);
 element('kind').value='wallpapers';await element('kind').onchange();assert.match(element('directory').textContent,/sdcard\/wallpapers/);assert.equal(element('file').accept,'.png,.jpg,.jpeg,.webp');
 element('file').files=[{name:'wallpaper.png',size:321}];const imageUpload=element('upload').onclick();await new Promise(setImmediate);assert.equal(lastXHR.path,'/api/upload?kind=wallpapers&directory=%2Fsdcard%2Fwallpapers&name=wallpaper.png');lastXHR.status=200;lastXHR.responseText='uploaded';lastXHR.onload();await imageUpload;
 await element('list').onclick();assert(requests.some(r=>r.path.startsWith('/api/list?kind=wallpapers')));
 await vm.runInContext("browse('/sdcard')",browser);assert.equal(vm.runInContext('target()',browser),'/sdcard');
 await vm.runInContext("browse('/sdcard/wallpapers/相册')",browser);
 vm.runInContext("selected=new Set(['owned.png'])",browser);await vm.runInContext('manage(false)',browser);
 const imageDelete=requests.filter(r=>r.path==='/api/manage').at(-1);assert.equal(JSON.parse(imageDelete.options.body).directory,'/sdcard/wallpapers/相册');
 await vm.runInContext("openPreview({name:'book.epub'})",browser);assert.equal(element('preview').open,true);assert.equal(element('preview-text').textContent,previewResponse.text);assert.equal(element('preview-next').disabled,false);
 await element('preview-next').onclick();assert(requests.at(-1).path.includes('chapter=1'));element('preview-close').onclick();assert.equal(element('preview').open,false);
 await vm.runInContext("openPreview({name:'picture.jpg'})",browser);assert.match(element('preview-image').src,/^blob:/);element('preview-close').onclick();
 assert(!html.includes('<section id="wifi">') && !html.includes("$('join').onclick") && !html.includes('Example.txt 仅'));
 const batchStart=requests.filter(r=>r.path==='/api/manage').length;
 vm.runInContext("selected=new Set(Array.from({length:130},(_,i)=>'owned-'+i+'.txt'))",browser);await vm.runInContext('manage(false)',browser);
 const batches=requests.filter(r=>r.path==='/api/manage').slice(batchStart).map(r=>JSON.parse(r.options.body).names.length);assert.deepEqual(batches,[64,64,2]);assert.match(element('result').textContent,/完成 130 项/);
 rescueUnauthorized=true;await assert.rejects(vm.runInContext('api("/api/status")',browser),/rescue token/);assert.equal(element('auth').hidden,false);
 console.log('Browser: upload abort, collision choices, Unicode rename job and same-version health verification passed');
})().catch(e=>{console.error(e);process.exitCode=1});
