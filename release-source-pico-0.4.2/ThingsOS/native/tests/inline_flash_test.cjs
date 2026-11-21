// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('ThingsOS/native/site/flash-engine.js','utf8').replace(/^import .*;$/gm,'').replace('export async function','async function');
const context=vm.createContext({Uint8Array,Error,setTimeout,clearTimeout});vm.runInContext(source,context);
async function run(fault='',chip='ESP32-S3'){
 const calls=[],events=[];const bytes=new Uint8Array([0xe9,1,2,3]).buffer;
 class Transport{async disconnect(){calls.push('close');if(fault==='close')throw Error('close failed');}}
 class ESPLoader{constructor(){this.chip={CHIP_NAME:chip}}async main(){calls.push('connect');if(fault==='connect')throw Error('connect failed')}async flashId(){calls.push('identify')}async eraseFlash(){calls.push('erase');if(fault==='erase')throw Error('erase failed')}async writeFlash(o){calls.push('write');assert.equal(o.fileArray[0].data.buffer,bytes);assert.equal(o.fileArray[0].address,0);assert.equal(o.eraseAll,false);o.reportProgress(0,25,100);if(fault==='write')throw Error('write failed');o.reportProgress(0,100,100)}}
 let error;try{await context.writeFirmware({},bytes,e=>events.push(e),{Transport,ESPLoader,async resetS3AfterInstall(){calls.push('reset');if(fault==='reset')throw Error('reset failed')}})}catch(e){error=e}
 return{calls,events,error};
}
(async()=>{
 let r=await run();assert(!r.error);assert.deepEqual(r.calls,['connect','identify','erase','write','reset','close']);assert.deepEqual(r.events.map(e=>e.stage),['connecting','erasing','writing','writing','writing','restarting','finished']);assert.equal(r.events[3].percentage,25);
 r=await run('','ESP32-C3');assert(r.error);assert.deepEqual(r.calls,['connect','close']);
 for(const fault of ['connect','erase','write','reset','close']){r=await run(fault);assert(r.error);assert.equal(r.calls.at(-1),'close');assert(!r.events.some(e=>e.stage==='finished'));if(['connect','erase','write'].includes(fault))assert(!r.calls.includes('reset'));}
 const fast=vm.createContext({Uint8Array,Error,setTimeout:fn=>setTimeout(fn,1),clearTimeout});vm.runInContext(source,fast);
 await assert.rejects(fast.closePort({disconnect:()=>new Promise(()=>{})}),e=>e.releasePending===true);
 console.log('PASS inline engine: verified bytes, true progress, chip guard before erase, stage failures, bounded cleanup and no false success.');
})().catch(e=>{console.error(e);process.exitCode=1});
