// SPDX-License-Identifier: Apache-2.0
// Verify bytes served by the production CDN, rather than merely the deploy response.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../build/native-site-current'),base=process.argv[2]||'https://thingsos.scholarcn.com/';
async function get(relative){const r=await fetch(new URL(relative,base),{cache:'no-store',signal:AbortSignal.timeout(60000)});assert.equal(r.status,200,relative);return r;}
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
(async()=>{
 const release=await(await get('release.json')).json();assert.deepEqual(release,JSON.parse(fs.readFileSync(path.join(root,'release.json'),'utf8')));
 const manifest=await(await get('manifest.json')).json();assert.deepEqual(manifest,JSON.parse(fs.readFileSync(path.join(root,'manifest.json'),'utf8')));
 assert.equal(release.version,'0.3.6');
 assert.equal(release.previous_version,'0.3.5');
 const prior=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../build/official-release-before-036.json'),'utf8'));
 assert.equal(prior.version,'0.3.5');assert.equal(release.previous_version,prior.version);
 assert.deepEqual(release.changes,JSON.parse(fs.readFileSync(path.resolve(__dirname,'release-notes.json'),'utf8')).changes);
 assert.equal(release.changes.length,7);assert(release.changes[0].includes('晃动'));
 const html=await(await get('')).text();assert(html.includes(release.version)&&html.includes('verify-release.js')&&!html.includes('history/')&&!html.includes('id="version">0.3.3'));
 assert(!html.match(/<header[^>]*>[\s\S]*?version-tag[\s\S]*?<\/header>/));
 assert(html.includes('相比 0.3.5 的更新') && html.includes('release-notes'));
 assert(release.changes.every(change=>html.includes(change)));
 const script=await(await get('verify-release.js')).text();assert.equal(script,fs.readFileSync(path.join(root,'verify-release.js'),'utf8'));
 assert(!html.includes('独立救援')&&!html.includes('验证范围'));
 const pageAssets=['styles.css','site.js','inline-installer.js','flash-engine.js','images/provenance.json',...fs.readdirSync(path.join(root,'images')).filter(n=>n.endsWith('.webp')).map(n=>'images/'+n),...fs.readdirSync(path.join(root,'images/brand')).map(n=>'images/brand/'+n)];
 await Promise.all(pageAssets.map(async item=>{const bytes=Buffer.from(await(await get(item)).arrayBuffer());assert.equal(hash(bytes),hash(fs.readFileSync(path.join(root,item))),item);}));
 assert(!html.includes('esp-web-install-button')&&!html.includes('vendor/install-button.js'));
 const engine=Buffer.from(await(await get(release.web_flasher_inline_engine.path)).arrayBuffer());assert.equal(hash(engine),release.web_flasher_inline_engine.sha256);
await get('THIRD_PARTY_NOTICES.txt');
 const bundle=Buffer.from(await(await get('vendor/install-dialog-im156JnI.js')).arrayBuffer());assert.equal(hash(bundle),release.web_flasher_reset.patched_sha256);
 const adapter=await(await get(release.web_flasher_reset.adapter)).text();assert.equal(adapter,fs.readFileSync(path.join(root,release.web_flasher_reset.adapter),'utf8'));
 const verified=await Promise.all(['usb','ota','source',...(release.sd_fonts?['sd_fonts']:[])].map(async kind=>{
  const item=release[kind],data=Buffer.from(await(await get(item.path)).arrayBuffer());assert.equal(data.length,item.size);assert.equal(hash(data),item.sha256);
  if(kind==='usb'){
   for(const p of release.parts){const offset=Number(p.offset);assert.equal(hash(data.subarray(offset,offset+p.size)),p.sha256);}
   assert.equal(data.readUInt32LE(0xf000),1);assert.equal(data.readUInt32LE(0xf000+24),0);assert.equal(data.readUInt32LE(0xf000+28),0x4743989a);
   assert.equal(hash(data.subarray(0x220000,0x220000+release.ota.size)),release.ota.sha256);
  }
  if(kind==='ota'){assert.equal(data.subarray(80,112).toString().split('\0')[0],'ThingsOS_native');assert.equal(data.subarray(48,80).toString().split('\0')[0],release.version);}
  return {kind,...item};
 }));
 const removed=['history/0.1.1/','history/0.1.1/release.json'];
 const oldReleasePath=path.resolve(__dirname,'../build/official-release-before-036.json');
 if(fs.existsSync(oldReleasePath)){const old=JSON.parse(fs.readFileSync(oldReleasePath,'utf8'));for(const k of ['usb','ota','source'])if(old[k]?.path&&!verified.some(item=>item.path===old[k].path))removed.push(old[k].path);}
 const oldFilesPath=path.resolve(__dirname,'../build/netlify-previous-files-010.json');
 if(fs.existsSync(oldFilesPath))for(const file of JSON.parse(fs.readFileSync(oldFilesPath,'utf8'))){const p=file.path.replace(/^\//,'');if((p.startsWith('history/')||/\.bin$/.test(p)||/^ThingsOS-.*-source\.zip$/.test(p))&&!fs.existsSync(path.join(root,p)))removed.push(p);}
 await Promise.all([...new Set(removed)].map(async url=>{const response=await fetch(new URL(url,base),{cache:'no-store',signal:AbortSignal.timeout(60000)});assert.equal(response.status,404,url);await response.body?.cancel();}));
 const report={url:base,checked_at:new Date().toISOString(),version:release.version,published:true,release_channel:'experimental',hardware_qualified:false,verified,page_assets_verified:pageAssets,removed_urls_return_404:removed};
 fs.writeFileSync(path.resolve(__dirname,`../build/native-deployment-verification-036-${new URL(base).hostname}.json`),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
})().catch(e=>{console.error(e);process.exitCode=1});
