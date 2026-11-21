// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
(async()=>{
 const src=fs.readFileSync(path.resolve(__dirname,'../site/thingsos-s3-reset.js'),'utf8');
 const {resetS3AfterInstall:reset}=await import('data:text/javascript;base64,'+Buffer.from(src).toString('base64'));
 async function run(failAt=0,error=Error('register failed')){const writes=[];let reported=false;const loader={chip:{CHIP_NAME:'ESP32-S3'},writeReg:async(...args)=>{writes.push(args);if(writes.length===failAt)throw error;},info:()=>reported=true};let failed=false;try{await reset(loader);}catch{failed=true;}return {writes,reported,failed};}
 let result=await run();assert.deepEqual(result.writes,[[0x6000812c,0,1],[0x600080b0,0x50d83aa1],[0x6000809c,2000],[0x60008098,0xd0000102]]);assert(result.reported&&!result.failed);
 for(let i=1;i<=4;i++){result=await run(i);assert(result.failed&&!result.reported);assert.equal(result.writes.length,i);}
 const disconnect=new Error('USB device disconnected');disconnect.name='NetworkError';result=await run(4,disconnect);assert(!result.failed&&result.reported);
 result=await run(3,disconnect);assert(result.failed&&!result.reported);
 result=await run(4,Error('Timeout'));assert(result.failed&&!result.reported);
 await assert.rejects(reset({chip:{CHIP_NAME:'ESP32-C3'},writeReg:()=>assert.fail('unsupported chip register write')}));
 console.log('PASS: S3 volatile reset sequence, fail-closed setup/timeouts, final USB disconnect, and chip guard.');
})().catch(e=>{console.error(e);process.exitCode=1});
