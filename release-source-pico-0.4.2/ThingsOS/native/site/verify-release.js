// SPDX-License-Identifier: Apache-2.0
// Keep installation unavailable until the actual downloaded binaries pass validation.
const consent=document.getElementById('consent'),install=document.getElementById('install'),status=document.getElementById('status');
let valid=false;
function sync(){install.disabled=!valid||!consent.checked||!globalThis.thingsOSInstallerReady||!!globalThis.thingsOSFlashBusy||(typeof navigator!=='undefined'&&(!globalThis.isSecureContext||!('serial' in navigator)));}
consent.addEventListener('change',sync);
globalThis.thingsOSSyncInstallation=sync;
async function json(path){const r=await fetch(path,{cache:'no-store'});if(!r.ok)throw Error('清单下载失败');return r.json();}
async function hash(bytes){return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');}
async function asset(info){if(!/^firmware\/[A-Za-z0-9_.-]+\.bin$/.test(info.path)||info.size<1||info.size>16*1024*1024)throw Error('固件路径或大小异常');const r=await fetch(info.path,{cache:'no-store'});if(!r.ok)throw Error('固件下载失败');const bytes=await r.arrayBuffer();if(bytes.byteLength!==info.size||await hash(bytes)!==info.sha256)throw Error('固件校验失败');return bytes;}
async function verifyInstallation(){try{
 const release=await json('release.json'),manifest=await json('manifest.json');
 if(release.schema!==2||release.board!=='RDP-G01-W'||release.project!=='ThingsOS_native'||release.layout!=='thingsos-native-single-v2'||release.status!=='release'||release.firmware_ready!==true)throw Error('设备或固件类型不匹配');
 if(manifest.version!==release.version||manifest.builds.length!==1||manifest.builds[0].chipFamily!=='ESP32-S3'||manifest.builds[0].parts.length!==1||manifest.builds[0].parts[0].offset!==0||manifest.builds[0].parts[0].path!==release.usb.path||manifest.new_install_prompt_erase!==true)throw Error('安装清单不匹配');
 const [usb,ota]=await Promise.all([asset(release.usb),asset(release.ota)]);
 const expected=[0,0x8000,0xf000,0x20000,0x220000,0xd00000];let end=0;
 if(release.parts.length!==expected.length)throw Error('完整包缺少必要分区');
 for(let i=0;i<expected.length;i++){const p=release.parts[i],offset=Number(p.offset);if(offset!==expected[i]||offset<end||p.size<1||offset+p.size>usb.byteLength||await hash(usb.slice(offset,offset+p.size))!==p.sha256)throw Error('安装包分区校验失败');end=offset+p.size;}
 const v=new DataView(usb);if(v.getUint32(0xf000,true)!==1||v.getUint32(0xf000+24,true)!==0||v.getUint32(0xf000+28,true)!==0x4743989a)throw Error('启动选择数据不匹配');
 const app=new Uint8Array(ota),text=(start,end)=>new TextDecoder().decode(app.slice(start,end)).split('\0')[0];
 if(app[0]!==0xe9||new DataView(ota).getUint16(12,true)!==9||text(80,112)!=='ThingsOS_native'||text(48,80)!==release.version||await hash(usb.slice(0x220000,0x220000+ota.byteLength))!==release.ota.sha256)throw Error('USB 与 OTA 应用不匹配');
 document.getElementById('version').textContent=release.version;document.getElementById('sha').textContent=release.usb.sha256;
 for(const kind of ['usb','ota']){const a=document.getElementById(kind);a.href=release[kind].path;a.download=release[kind].path.split('/').pop();}
 for(const [kind,label]of [['sd_fonts','更多字体']])if(release[kind]){const a=document.createElement('a');a.href=release[kind].path;a.className='download';a.textContent=label;a.download=release[kind].path;document.getElementById('downloads').append(a);}
 document.getElementById('downloads').hidden=false;globalThis.thingsOSVerifiedInstallation=Object.freeze({usb});valid=true;status.textContent='安装包校验通过。确认型号并勾选上方选项，即可连接设备。';sync();
 }catch(error){valid=false;sync();status.textContent=error.message+'。安装入口已停用，请刷新后重试。';}}
function watchInstallation(){const observer=new IntersectionObserver(entries=>{if(entries.some(e=>e.isIntersecting)){observer.disconnect();verifyInstallation();}},{rootMargin:'500px'});observer.observe(document.getElementById('installer'));}
// Keep feature browsing light; download and verify firmware near installation.
('IntersectionObserver' in globalThis ? watchInstallation() : verifyInstallation());
