// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
(async()=>{
 const root=path.resolve(__dirname,'../../build/native-site-current/vendor');
 const source=fs.readFileSync(path.join(root,'install-dialog-im156JnI.js'),'utf8');
 assert(source.startsWith('import {resetS3AfterInstall as thingsosReset}'));
 const begin=source.indexOf('async after('),end=source.indexOf('}class Ss',begin);assert(begin>=0&&end>begin);
 const adapter=fs.readFileSync(path.join(root,'thingsos-s3-reset.js'),'utf8');
 const {resetS3AfterInstall}=await import('data:text/javascript;base64,'+Buffer.from(adapter).toString('base64'));
 const Loader=vm.runInNewContext('class Loader {'+source.slice(begin,end)+'};Loader',{thingsosReset:resetS3AfterInstall});
 const writes=[];let rts=0;
 const l=new Loader();Object.assign(l,{chip:{CHIP_NAME:'ESP32-S3'},writeReg:async(...v)=>writes.push(v),info:()=>{},resetConstructors:{hardReset:()=>({reset:async()=>rts++})}});
 await l.after('hard_reset');assert.equal(writes.length,4);assert.equal(rts,0);
 const finishStart=source.indexOf('wa=async(e,t)=>'),finishEnd=source.indexOf(',Ea=',finishStart);assert(finishStart>=0&&finishEnd>finishStart);
 const finish=vm.runInNewContext(source.slice(finishStart+3,finishEnd),{xe:async()=>{}});
 let signals=0;await finish({setRTS:async()=>signals++},l);assert.equal(signals,0);assert.equal(writes.length,8);
 l.chip.CHIP_NAME='ESP32-C3';await finish({setRTS:async()=>signals++},l);assert.equal(signals,1);assert.equal(rts,1);assert.equal(writes.length,8);
 l.chip.CHIP_NAME='ESP32-S3';l.writeReg=async()=>{throw Error('register failed');};await assert.rejects(l.after('hard_reset'));assert.equal(rts,1);
 console.log('PASS: actual packaged ESPLoader reset dispatch uses S3 adapter, preserves other targets, propagates failure.');
})().catch(e=>{console.error(e);process.exitCode=1});
