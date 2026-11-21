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
 assert.equal(release.version,'0.4.1');assert.equal(release.status,'release');assert.equal(release.firmware_ready,true);
 assert.equal(release.distribution,'firmware-only');assert(!release.source&&!release.sd_fonts);
 assert(!fs.readdirSync(root).some(name=>name.endsWith('.zip')));assert(!fs.existsSync(path.join(root,'history')));
 const binaries=fs.readdirSync(path.join(root,'firmware')).filter(name=>fs.statSync(path.join(root,'firmware',name)).isFile());assert.deepEqual(binaries.sort(),[path.basename(release.usb.path),path.basename(release.ota.path)].sort());
 const html=fs.readFileSync(path.join(root,'index.html'),'utf8');assert(!html.includes('history/'));
 assert(!html.match(/<header[^>]*>[\s\S]*?version-tag[\s\S]*?<\/header>/));
 assert(html.includes('id="version">0.4.1')&&html.includes('相比 0.4.0 的更新')&&html.includes('release-notes'));
 assert.equal(release.previous_version,'0.4.0');assert.equal(release.comparison_version,'0.4.0');
 assert.deepEqual(release.changes,JSON.parse(fs.readFileSync(path.resolve(__dirname,'../release-notes.json'),'utf8')).changes);
 assert(release.changes[0].includes('EPUB') && release.changes.some(c=>c.includes('WebP')));
 assert(release.changes.some(c=>c.includes('充电标识延迟')&&c.includes('E-Ink4-Plus')));
 assert.equal(release.charge_status_validation.application_sha256,release.ota.sha256);
 assert.equal(release.charge_status_validation.rescue_sha256,release.rescue.sha256);
 assert.equal(release.charge_status_validation.exact_release_image_flashed,false);
 assert(Buffer.byteLength(release.changes.join('\n'),'utf8')<1024);
 assert(fs.statSync(path.join(root,'release.json')).size<=16384);
 assert(release.changes.every(change=>html.includes(change)));
 const history=JSON.parse(fs.readFileSync(path.join(root,release.release_history),'utf8'));
 assert.equal(history.current_version,release.version);assert.equal(history.releases[0].version,release.previous_version);
 assert(history.releases.length>=6);assert(html.includes('仅保留更新日志'));
 for(const entry of history.releases){
  assert.notEqual(entry.version,release.version);assert(html.includes('v'+entry.version));
  assert(Object.keys(entry).every(k=>['version','previous_version','date','changes'].includes(k)));
  assert(entry.changes.every(change=>html.includes(change.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;'))));
 }
 const historySection=html.slice(html.indexOf('<section id="release-history"'),html.indexOf('<section class="faq '));
 assert.equal((historySection.match(/<details>/g)||[]).length,history.releases.length+(history.metalio?.releases.length||0));
 assert(!/href=|\.bin|\.zip/.test(historySection));

 const preserved=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../../build/release-041-metalio-preservation.json'),'utf8'));
 const metalio=JSON.parse(fs.readFileSync(path.join(root,'metalio/release.json'),'utf8'));
 assert.equal(metalio.version,'0.1.7');
 assert.deepEqual(history.metalio,JSON.parse(fs.readFileSync(path.resolve(__dirname,'../../build/publish-pico-0.4.1/official-release-history.json'),'utf8')).metalio);
 assert.equal(metalio.version,preserved.version);assert.equal(metalio.status,'release');assert.equal(metalio.firmware_ready,true);
 for(const item of preserved.files){const bytes=fs.readFileSync(path.join(root,item.path));assert.equal(bytes.length,item.size);assert.equal(require('node:crypto').createHash('sha256').update(bytes).digest('hex'),item.sha256,item.path);}
 let ids=await scenario();assert.equal(ids.install.disabled,false);assert.equal(ids.downloads.hidden,false);assert(ids.version.textContent.includes(release.version));
 assert(!ids.downloads.children.some(a=>a.href===release.source?.path||a.textContent.includes('源码')));
 ids=await scenario({apply:r=>{r.status='experimental-not-hardware-qualified';r.firmware_ready=false;}});assert.equal(ids.install.disabled,true);
 ids=await scenario({},release.usb.path);assert.equal(ids.install.disabled,true);assert(ids.status.textContent.includes('校验失败'));
 ids=await scenario({},release.ota.path);assert.equal(ids.install.disabled,true);
 ids=await scenario({apply:(r)=>r.project='ThingsOS_ReadPico_R1'});assert.equal(ids.install.disabled,true);
 ids=await scenario({apply:(r)=>r.parts[3].offset='0x10000'});assert.equal(ids.install.disabled,true);
 ids=await scenario({apply:(r,m)=>m.builds[0].parts[0].path='firmware/old.bin'});assert.equal(ids.install.disabled,true);
 console.log('PASS: actual staged USB/OTA parts and bootstrap; corrupted download, old project, wrong offsets and mismatched manifest disable installation.');
})().catch(e=>{console.error(e);process.exitCode=1});
