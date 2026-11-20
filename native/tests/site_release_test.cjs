// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../build/native-site-current');
const script=fs.readFileSync(path.join(root,'verify-release.js'),'utf8');
const release=JSON.parse(fs.readFileSync(path.join(root,'release.json'),'utf8'));
const manifest=JSON.parse(fs.readFileSync(path.join(root,'manifest.json'),'utf8'));
async function scenario(change={},corrupt=''){
 const r=structuredClone(release),m=structuredClone(manifest);if(change.apply)change.apply(r,m);
 const ids={};function element(){return{disabled:true,hidden:true,checked:true,textContent:'',children:[],addEventListener(){},append(v){this.children.push(v)}};}
 for(const id of ['consent','install','status','version','sha','usb','ota','downloads'])ids[id]=element();
 const context=vm.createContext({thingsOSInstallerReady:true,document:{getElementById:id=>ids[id],createElement:element},TextDecoder,DataView,Uint8Array,Array,Error,Promise,crypto:require('node:crypto').webcrypto,
 fetch:async url=>{let bytes;if(url==='release.json')bytes=Buffer.from(JSON.stringify(r));else if(url==='manifest.json')bytes=Buffer.from(JSON.stringify(m));else{bytes=Buffer.from(fs.readFileSync(path.join(root,url)));if(url===corrupt)bytes[100]^=1;}return{ok:true,json:async()=>JSON.parse(bytes.toString()),arrayBuffer:async()=>Uint8Array.from(bytes).buffer};}});
 await vm.runInContext(script,context);return ids;
}
(async()=>{
 assert.equal(release.version,'0.3.6');assert(!fs.existsSync(path.join(root,'history')));
 const binaries=fs.readdirSync(path.join(root,'firmware'));assert.deepEqual(binaries.sort(),[path.basename(release.usb.path),path.basename(release.ota.path)].sort());
 const html=fs.readFileSync(path.join(root,'index.html'),'utf8');assert(!html.includes('history/'));
 assert(!html.match(/<header[^>]*>[\s\S]*?version-tag[\s\S]*?<\/header>/));
 assert(html.includes('id="version">0.3.6')&&html.includes('相比 0.3.5 的更新')&&html.includes('release-notes'));
 assert.equal(release.previous_version,'0.3.5');assert.equal(release.changes.length,7);
 assert.deepEqual(release.changes,JSON.parse(fs.readFileSync(path.resolve(__dirname,'../release-notes.json'),'utf8')).changes);
 assert(release.changes[0].includes('晃动') && release.changes[6].includes('锁屏'));
 assert(Buffer.byteLength(release.changes.join('\n'),'utf8')<1024);
 assert(fs.statSync(path.join(root,'release.json')).size<=16384);
 assert(release.changes.every(change=>html.includes(change)));
 let ids=await scenario();assert.equal(ids.install.disabled,false);assert.equal(ids.downloads.hidden,false);assert(ids.version.textContent.includes(release.version));
 assert(!ids.downloads.children.some(a=>a.href===release.source.path||a.textContent.includes('源码')));
 ids=await scenario({},release.usb.path);assert.equal(ids.install.disabled,true);assert(ids.status.textContent.includes('校验失败'));
 ids=await scenario({},release.ota.path);assert.equal(ids.install.disabled,true);
 ids=await scenario({apply:(r)=>r.project='ThingsOS_ReadPico_R1'});assert.equal(ids.install.disabled,true);
 ids=await scenario({apply:(r)=>r.parts[3].offset='0x10000'});assert.equal(ids.install.disabled,true);
 ids=await scenario({apply:(r,m)=>m.builds[0].parts[0].path='firmware/old.bin'});assert.equal(ids.install.disabled,true);
 console.log('PASS: actual staged USB/OTA parts and bootstrap; corrupted download, old project, wrong offsets and mismatched manifest disable installation.');
})().catch(e=>{console.error(e);process.exitCode=1});
