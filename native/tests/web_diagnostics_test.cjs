// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/../firmware/components/thingsos_net/web.html','utf8');
const start=html.indexOf("$('download-diagnostics').onclick="),end=html.indexOf('\nfunction target()',start);
assert(start>0&&end>start);const source=html.slice(start,end);
const nodes=new Map();const $=id=>{if(!nodes.has(id))nodes.set(id,{disabled:false,textContent:''});return nodes.get(id)};
let fail=false,clicked=0,removed=0,revoked=0,requests=0;
const link={click(){++clicked},remove(){++removed}};
const ctx=vm.createContext({$,active:false,api:async path=>{assert.equal(path,'/api/diagnostics');++requests;if(fail)throw Error('Device busy; retry later');return {blob:async()=>new Blob(['{}'])}},
 document:{createElement:()=>link,body:{append(){}}},URL:{createObjectURL:()=> 'blob:diagnostic',revokeObjectURL:()=>++revoked},setTimeout:fn=>fn()});
vm.runInContext(source,ctx);
(async()=>{
 await $('download-diagnostics').onclick();assert.equal(requests,1);assert.equal(clicked,1);assert.equal(removed,1);assert.equal(revoked,1);
 assert.equal(link.download,'ThingsOS-diagnostics.json');assert.equal($('download-diagnostics').disabled,false);
 fail=true;await $('download-diagnostics').onclick();assert.equal(clicked,1);assert.equal($('diagnostic-result').textContent,'Device busy; retry later');assert.equal($('download-diagnostics').disabled,false);
 assert(html.includes('id="download-diagnostics"') && html.includes('state.rescue_reason'));
 console.log('PASS: actual diagnostic download handler, blob cleanup, busy failure recovery, rescue reason UI');
})().catch(e=>{console.error(e);process.exitCode=1});
