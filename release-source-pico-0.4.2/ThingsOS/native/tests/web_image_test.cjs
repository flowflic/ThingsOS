// SPDX-License-Identifier: Apache-2.0
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),zlib=require('node:zlib'),path=require('node:path');
let dimensions=[3000,4000],closed=0,jpegSizes=[300000],qualities=[],canvases=[];
function canvas(){const c={width:0,height:0,operations:[],getContext(){return {fillRect(...a){c.operations.push(['white',...a])},drawImage(...a){c.operations.push(['draw',...a.slice(1)])},getImageData(){const data=new Uint8ClampedArray(c.width*c.height*4);for(let i=0;i<data.length;i+=4){data[i]=255;data[i+1]=((i/4)%c.width)*255/c.width;data[i+2]=Math.floor((i/4)/c.width)*255/c.height;data[i+3]=255}return {data}},putImageData(){}}},toBlob(fn,type,quality){qualities.push(quality);fn(new Blob([new Uint8Array(jpegSizes.length>1?jpegSizes.shift():jpegSizes[0])],{type}))}};canvases.push(c);return c}
const context=vm.createContext({Blob,File,CompressionStream,Response,DataView,Uint8Array,Uint8ClampedArray,Math,Date,URL,document:{createElement:()=>canvas()},createImageBitmap:async(file,options)=>{assert.equal(options.imageOrientation,'from-image');return {width:dimensions[0],height:dimensions[1],close(){closed++}}}});
vm.runInContext(fs.readFileSync(__dirname+'/../firmware/components/thingsos_net/web_image.js','utf8'),context);
async function prepare(name,wallpaper=false){context.input=new File([new Uint8Array([1,2,3])],name);context.wallpaper=wallpaper;return vm.runInContext('thingsos_prepare_image(input,wallpaper)',context)}
async function png(result,w,h){const data=Buffer.from(await result.file.arrayBuffer());assert.equal(data.readUInt32BE(16),w);assert.equal(data.readUInt32BE(20),h);assert.equal(data[24],8);assert.equal(data[25],0);let at=8,idat=[];while(at<data.length){let n=data.readUInt32BE(at),type=data.toString('ascii',at+4,at+8);if(type==='IDAT')idat.push(data.subarray(at+8,at+8+n));at+=n+12}const raw=zlib.inflateSync(Buffer.concat(idat));assert.equal(raw.length,(w+1)*h);assert.equal(raw[0],0);assert.equal(raw[1],76);assert(data.length<=1024*1024);return data}
(async()=>{
 const portrait=await prepare('竖图.png');assert.match(portrait.info,/684×1216/);const portraitPng=await png(portrait,684,1216);
 assert.deepEqual(canvases[0].operations[0],['white',0,0,684,1216]);assert.deepEqual(canvases[0].operations[1],['draw',375,0,2250,4000,0,0,684,1216]);assert.equal(canvases[0].width,1);assert.equal(closed,1);
 dimensions=[4000,3000];const landscape=await prepare('横图.png');const landscapePng=await png(landscape,1216,684);assert.deepEqual(canvases[1].operations[1],['draw',0,375,4000,2250,0,0,1216,684]);
 qualities=[];jpegSizes=[900000,600000];const jpeg=await prepare('camera.JPEG');assert.equal(jpeg.file.name,'camera.jpg');assert.equal(jpeg.file.size,600000);assert.deepEqual(qualities,[0.86,0.78]);
 context.CompressionStream=undefined;jpegSizes=[200000];const fallback=await prepare('alpha.png');assert.equal(fallback.file.name,'alpha.jpg');context.CompressionStream=CompressionStream;
 context.input=new File(['font'],'font.ttf');const untouched=await vm.runInContext('thingsos_prepare_image(input)',context);assert.equal(untouched.file,context.input);
 jpegSizes=[2*1024*1024];await assert.rejects(prepare('too-large.jpg'),/压缩失败/);assert.equal(canvases.at(-1).width,1);assert.equal(closed,5);
 dimensions=[3000,4000];jpegSizes=[200000];const wallpaper=await prepare('portrait.webp',true);
 assert.equal(wallpaper.file.name,'portrait.jpg');assert.match(wallpaper.info,/完整宽度/);
 assert.deepEqual(canvases.at(-1).operations[1],['draw',0,0,3000,4000,0,152,684,912]);
 dimensions=[4000,3000];await prepare('landscape.webp',true);
 assert.deepEqual(canvases.at(-1).operations[1],['draw',0,375,4000,2250,0,0,1216,684]);
 if(process.argv[2]){const root=path.resolve(process.argv[2]),dir=path.join(root,'wallpapers');fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'portrait.png'),portraitPng);fs.writeFileSync(path.join(dir,'landscape.png'),landscapePng);fs.copyFileSync(path.join(root,'picture.jpg'),path.join(dir,'picture.jpg'));fs.writeFileSync(path.join(dir,'bad.png'),Buffer.from([1,2,3]));}
 console.log('Image preparation: oriented center crops, grayscale PNG decode, alpha white base, JPEG quality fallback, cleanup and unchanged fonts passed');
})().catch(e=>{console.error(e);process.exitCode=1});
