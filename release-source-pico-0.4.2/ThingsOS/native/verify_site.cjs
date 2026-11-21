// SPDX-License-Identifier: Apache-2.0
// Verify bytes served by the production CDN, rather than merely the deploy response.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../build/native-site-current'),base=process.argv[2]||'https://thingsos.scholarcn.com/';
async function get(relative){const r=await fetch(new URL(relative,base),{cache:'no-store',signal:AbortSignal.timeout(60000)});assert.equal(r.status,200,relative);return r;}
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
(async()=>{
 const release=await(await get('release.json')).json();assert.deepEqual(release,JSON.parse(fs.readFileSync(path.join(root,'release.json'),'utf8')));
 const manifest=await(await get('manifest.json')).json();assert.deepEqual(manifest,JSON.parse(fs.readFileSync(path.join(root,'manifest.json'),'utf8')));
 assert.equal(release.version,'0.4.1');assert.equal(release.status,'release');assert.equal(release.firmware_ready,true);
 assert.equal(release.distribution,'firmware-only');assert(!release.source&&!release.sd_fonts);
 assert(!fs.readdirSync(root).some(name=>name.endsWith('.zip')));
 assert.equal(release.previous_version,'0.4.0');assert.equal(release.comparison_version,'0.4.0');
 const prior=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../build/official-release-before-041.json'),'utf8'));
 assert.equal(prior.version,'0.4.0');assert.equal(release.previous_version,prior.version);
 assert.deepEqual(release.changes,JSON.parse(fs.readFileSync(path.resolve(__dirname,'release-notes.json'),'utf8')).changes);
 assert.equal(release.charge_status_validation.application_sha256,release.ota.sha256);
 assert.equal(release.charge_status_validation.rescue_sha256,release.rescue.sha256);
 assert.equal(release.changes.length,5);assert(release.changes[0].includes('EPUB'));
 const html=await(await get('')).text();assert(html.includes(release.version)&&html.includes('verify-release.js')&&!html.includes('history/')&&!html.includes('id="version">0.3.3'));
 assert(!html.match(/<header[^>]*>[\s\S]*?version-tag[\s\S]*?<\/header>/));
 assert(html.includes('相比 0.4.0 的更新') && html.includes('release-notes'));
 assert(release.changes.every(change=>html.includes(change)));
 const history=await(await get(release.release_history)).json();
 assert.deepEqual(history,JSON.parse(fs.readFileSync(path.join(root,'release-history.json'),'utf8')));
 const baselineHistory=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../build/publish-pico-0.4.1/official-release-history.json'),'utf8'));
 assert.deepEqual(history.metalio,baselineHistory.metalio);
 const section=html.slice(html.indexOf('<section id="release-history"'),html.indexOf('<section class="faq '));
 assert.equal((section.match(/<details>/g)||[]).length,history.releases.length+(history.metalio?.releases.length||0));
 assert(!/href=|\.bin|\.zip/.test(section));
 for(const entry of history.releases)assert(section.includes('v'+entry.version));

 const script=await(await get('verify-release.js')).text();assert.equal(script,fs.readFileSync(path.join(root,'verify-release.js'),'utf8'));
 assert(!html.includes('独立救援')&&!html.includes('验证范围'));
 const pageAssets=['release-history.json','styles.css','site.js','inline-installer.js','flash-engine.js','images/provenance.json',...fs.readdirSync(path.join(root,'images')).filter(n=>n.endsWith('.webp')).map(n=>'images/'+n),...fs.readdirSync(path.join(root,'images/brand')).map(n=>'images/brand/'+n)];
 await Promise.all(pageAssets.map(async item=>{const bytes=Buffer.from(await(await get(item)).arrayBuffer());assert.equal(hash(bytes),hash(fs.readFileSync(path.join(root,item))),item);}));
 assert(!html.includes('esp-web-install-button')&&!html.includes('vendor/install-button.js'));
 const engine=Buffer.from(await(await get(release.web_flasher_inline_engine.path)).arrayBuffer());assert.equal(hash(engine),release.web_flasher_inline_engine.sha256);
await get('THIRD_PARTY_NOTICES.txt');
 const bundle=Buffer.from(await(await get('vendor/install-dialog-im156JnI.js')).arrayBuffer());assert.equal(hash(bundle),release.web_flasher_reset.patched_sha256);
 const adapter=await(await get(release.web_flasher_reset.adapter)).text();assert.equal(adapter,fs.readFileSync(path.join(root,release.web_flasher_reset.adapter),'utf8'));
 const verified=await Promise.all(['usb','ota'].map(async kind=>{
  const item=release[kind],data=Buffer.from(await(await get(item.path)).arrayBuffer());assert.equal(data.length,item.size);assert.equal(hash(data),item.sha256);
  if(kind==='usb'){
   for(const p of release.parts){const offset=Number(p.offset);assert.equal(hash(data.subarray(offset,offset+p.size)),p.sha256);}
   assert.equal(data.readUInt32LE(0xf000),1);assert.equal(data.readUInt32LE(0xf000+24),0);assert.equal(data.readUInt32LE(0xf000+28),0x4743989a);
   assert.equal(hash(data.subarray(0x220000,0x220000+release.ota.size)),release.ota.sha256);
  }
  if(kind==='ota'){assert.equal(data.subarray(80,112).toString().split('\0')[0],'ThingsOS_native');assert.equal(data.subarray(48,80).toString().split('\0')[0],release.version);}
  return {kind,...item};
 }));
 const removed=['history/0.1.1/','history/0.1.1/release.json','ThingsOS-SD-fonts.zip',`ThingsOS-${release.version}-${release.ota.sha256.slice(0,12)}-source.zip`];
 if(process.env.THINGSOS_SUPERSEDED_RELEASE){
  const old=JSON.parse(fs.readFileSync(path.resolve(process.env.THINGSOS_SUPERSEDED_RELEASE),'utf8'));
  assert.notEqual(old.ota.sha256,release.ota.sha256,'Replacement must contain the new firmware');
  for(const k of ['usb','ota'])if(old[k]?.path)removed.push(old[k].path);
 }
 for(const filename of ['official-release-before-037.json','official-release-before-038.json','official-release-before-039.json','official-release-before-040.json','official-release-before-041.json']) {
  const oldReleasePath=path.resolve(__dirname,'../build',filename);
  if(fs.existsSync(oldReleasePath)){const old=JSON.parse(fs.readFileSync(oldReleasePath,'utf8'));for(const k of ['usb','ota','source'])if(old[k]?.path&&!verified.some(item=>item.path===old[k].path))removed.push(old[k].path);}
 }
 const oldFilesPath=path.resolve(__dirname,'../build/netlify-previous-files-010.json');
 if(fs.existsSync(oldFilesPath))for(const file of JSON.parse(fs.readFileSync(oldFilesPath,'utf8'))){const p=file.path.replace(/^\//,'');if((p.startsWith('history/')||/\.bin$/.test(p)||/^ThingsOS-.*-source\.zip$/.test(p))&&!fs.existsSync(path.join(root,p)))removed.push(p);}
 await Promise.all([...new Set(removed)].map(async url=>{const response=await fetch(new URL(url,base),{cache:'no-store',signal:AbortSignal.timeout(60000)});assert.equal(response.status,404,url);await response.body?.cancel();}));
 const preserved=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../build/release-041-metalio-preservation.json'),'utf8'));
 await Promise.all(preserved.files.map(async item=>{const bytes=Buffer.from(await(await get(item.path)).arrayBuffer());assert.equal(bytes.length,item.size);assert.equal(hash(bytes),item.sha256,item.path);}));
 const report={url:base,checked_at:new Date().toISOString(),version:release.version,published:true,release_channel:'release',hardware_qualified:false,verified,page_assets_verified:pageAssets,metalio_version:preserved.version,metalio_assets_verified:preserved.files.length,removed_urls_return_404:removed};
 const reportPath=process.env.THINGSOS_VERIFICATION_REPORT||path.resolve(__dirname,`../build/native-deployment-verification-041-${new URL(base).hostname}.json`);
 fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1});
