// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/../firmware/components/thingsos_ima/ima.html','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
assert(!/id="(?:password|unlock)"/.test(html));
assert(!script.includes('https://'));
function response(ok,text,state){return {ok,text:async()=>text||'',json:async()=>state};}
async function scenario(sequence){
 const messages=[],requests=[],nodes={};
 for(const id of ['form','code','client','key','send','result'])nodes[id]={value:id==='code'?'12345678':id==='client'?' client-id ':id==='key'?' api-key ':'',dataset:{},disabled:false};
 Object.defineProperty(nodes.result,'textContent',{set(value){messages.push(value);this.text=value},get(){return this.text;}});
 const context=vm.createContext({document:{querySelector:selector=>nodes[selector.slice(1)]},AbortController,
  setTimeout:(fn,ms)=>ms===1000?(queueMicrotask(fn),0):setTimeout(fn,ms),clearTimeout,
  fetch:async(path,options)=>{requests.push({path,options});assert(sequence.length,'Unexpected extra request');const next=sequence.shift();if(next instanceof Error)throw next;return next;}});
 vm.runInContext(script,context);await nodes.form.onsubmit({preventDefault(){}});
 assert.equal(nodes.key.value,'');assert.equal(sequence.length,0);
 const payload=JSON.parse(requests[0].options.body);
 assert.deepEqual(payload,{code:'12345678',clientId:'client-id',apiKey:'api-key'});
 for(const request of requests.slice(1)){assert.equal(request.path,'/configure/status');assert.equal(request.options.headers['X-ThingsOS-Pair-Code'],'12345678');}
 return {nodes,messages,requests};
}
(async()=>{
 let r=await scenario([response(true,'accepted'),response(true,'',{phase:'verifying',message:'Checking knowledge bases'}),response(true,'',{phase:'ready',message:'Connected; choose a library'})]);
 assert.equal(r.nodes.result.dataset.phase,'ready');assert(r.nodes.send.disabled);assert(r.messages.includes('Checking knowledge bases'));assert.equal(r.messages.at(-1),'Connected; choose a library');
 r=await scenario([response(true,'accepted'),response(true,'',{phase:'failed',message:'ima HTTP 403'})]);
 assert.equal(r.nodes.result.dataset.phase,'failed');assert.equal(r.messages.at(-1),'ima HTTP 403');assert(!r.nodes.send.disabled);
 r=await scenario([response(false,'Pairing code invalid')]);assert.equal(r.messages.at(-1),'Pairing code invalid');assert(!r.nodes.send.disabled);
 r=await scenario([response(true,'accepted'),response(false,'Pairing expired')]);assert.equal(r.messages.at(-1),'Pairing expired');
 r=await scenario([response(true,'accepted'),new Error('Device disconnected')]);assert.equal(r.messages.at(-1),'Device disconnected');
 r=await scenario([response(true,'accepted'),...Array.from({length:90},()=>response(true,'',{phase:'verifying',message:'Still checking'}))]);
 assert.equal(r.nodes.result.dataset.phase,'failed');assert(r.messages.at(-1).includes('勿重复提交'));assert(!r.nodes.send.disabled);
 console.log('Actual IMA form: no unlock password; accepted-to-ready/failed, rejected code, expiry, disconnect, bounded waiting and API-key clearing passed');
})().catch(error=>{console.error(error);process.exit(1)});
