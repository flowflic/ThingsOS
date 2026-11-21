// SPDX-License-Identifier: Apache-2.0
// Executes the actual phone-page script with deterministic HTTP/DOM edges.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/../firmware/components/thingsos_net/web.html','utf8');
const source=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].at(-1)[1];
const nodes=new Map(),requests=[],uploads=[],responses=[];let collision='both',imageCalls=0,lastXHR;
function element(id){if(!nodes.has(id))nodes.set(id,{id,value:id==='kind'?'books':'',files:[],hidden:false,disabled:false,dataset:{},style:{},classList:{toggle(){}},replaceChildren(){},removeAttribute(){},append(){},prepend(){},close(){},querySelectorAll(selector){return selector==='[data-collision]'?choices:[];},showModal(){queueMicrotask(()=>element(collision).onclick());}});return nodes.get(id);}
const tabs=['books','ota','usb'].map(tab=>{const e=element('tab-'+tab);e.dataset.tab=tab;return e;});
const choices=['cancel','both','replace'].map(choice=>{const e=element(choice);e.dataset.collision=choice;return e;});
const document={getElementById:element,createElement:()=>({append(){},prepend(){}}),querySelectorAll(selector){if(selector==='[data-tab]')return tabs;if(selector==='[data-collision]')return choices;return [...nodes.values()];}};
class XHR{constructor(){this.upload={};this.headers={};lastXHR=this;}open(method,path){this.method=method;this.path=path;}setRequestHeader(k,v){this.headers[k]=v;}send(file){uploads.push({method:this.method,path:this.path,file,headers:this.headers});const next=responses.shift()||200;if(next==='pending')return;this.status=next;this.responseText=next===409?'Name exists':'Uploaded';queueMicrotask(()=>this.onload());}abort(){this.onabort();}}
const context=vm.createContext({document,window:{addEventListener(){}},location:{search:'?kind=music'},localStorage:{getItem(){return null;},setItem(){},removeItem(){}},URLSearchParams,URL,XMLHttpRequest:XHR,console,queueMicrotask,setTimeout,clearTimeout,
 thingsos_prepare_image:async file=>{++imageCalls;return {file,info:''};},
 fetch:async path=>{requests.push(path);const kind=new URL('http://device'+path).searchParams.get('kind');return {ok:true,status:200,headers:{get(key){return key==='X-ThingsOS-Directory'?encodeURIComponent(kind==='music'?'/sdcard/music':'/sdcard/MyBooks'):key==='X-ThingsOS-Card'?'1':null;}},json:async()=>path==='/api/status'?{version:'0.3.6',sd:true,wifi:true,rescue:false,library:'/sdcard/MyBooks'}:[],text:async()=>''};}});
const settle=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{vm.runInContext(fs.readFileSync(__dirname+'/../firmware/components/thingsos_net/web_image.js','utf8'),context);const prepare=context.thingsos_prepare_image;context.thingsos_prepare_image=async file=>{++imageCalls;return prepare(file);};vm.runInContext(source,context);await settle();await settle();
 assert.equal(element('kind').value,'music');assert.equal(element('file').accept,'.mp3,.wav');assert.equal(element('sd-root').hidden,true);assert.equal(element('parent').hidden,true);
 assert.match(requests.find(p=>p.startsWith('/api/list')),/kind=music/);
 vm.runInContext("currentDirectory='/sdcard/wrong'",context);
 let query=new URLSearchParams(vm.runInContext("fileQuery('轻音乐.mp3')",context));assert.equal(query.get('directory'),'/sdcard/music');assert.equal(query.get('kind'),'music');
 const music={name:'轻音乐.MP3',size:4096};element('file').files=[music];await element('upload').onclick();
 assert.equal(uploads.length,1);assert.equal(uploads[0].file,music);query=new URL('http://device'+uploads[0].path).searchParams;assert.equal(query.get('directory'),'/sdcard/music');assert.equal(query.get('kind'),'music');
 element('file').files=[{name:'wrong.jpg'}];const before=imageCalls;await element('upload').onclick();assert.equal(uploads.length,1);assert.equal(imageCalls,before);assert.match(element('result').textContent,/MP3 \/ WAV/);
 element('file').files=[music];responses.push(409,200);await element('upload').onclick();assert.equal(uploads.length,3);assert.equal(uploads.at(-1).headers['X-ThingsOS-Collision'],'both');assert.equal(uploads.at(-1).path,uploads.at(-2).path);
 vm.runInContext("fileItems=[{name:'轻音乐.mp3'}]",context);
 const knownCount=uploads.length;await element('upload').onclick();assert.equal(uploads.length,knownCount+1);assert.equal(uploads.at(-1).headers['X-ThingsOS-Collision'],'both');
 collision='cancel';const cancelCount=uploads.length;await element('upload').onclick();assert.equal(uploads.length,cancelCount);assert.match(element('result').textContent,/原文件保留/);collision='both';
 vm.runInContext('fileItems=[]',context);
 element('progress').value=57;responses.push('pending');const pending=element('upload').onclick();await settle();assert.equal(element('progress').value,0);assert.match(element('result').textContent,/正在发送/);
 lastXHR.upload.onprogress({lengthComputable:true,loaded:1024,total:4096});assert.equal(element('progress').value,25);assert.match(element('result').textContent,/发送文件 25%/);
 lastXHR.upload.onprogress({lengthComputable:true,loaded:4096,total:4096});assert.match(element('result').textContent,/等待设备保存结果/);
 element('abort-upload').onclick();await pending;assert.match(element('result').textContent,/已取消上传/);assert.equal(element('kind').disabled,false);
 element('kind').value='books';await element('kind').onchange();assert.equal(element('sd-root').hidden,false);assert.equal(element('file').accept,'.epub,.txt,.md,.markdown,.png,.jpg,.jpeg,.webp');query=new URLSearchParams(vm.runInContext("fileQuery('book.epub')",context));assert.equal(query.get('directory'),'/sdcard/MyBooks');assert.equal(query.get('kind'),'books');
 console.log('PASS: actual phone upload script: music query/accept/fixed directory/raw bytes, wrong format rejection, collision retry, cancellation and custom book-library preservation');
})().catch(e=>{console.error(e);process.exitCode=1;});
