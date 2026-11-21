// SPDX-License-Identifier: Apache-2.0
/* Local image preparation; no image or metadata is sent before it succeeds. */
function thingsos_image_crop(width,height,wallpaper=false){
 if(!(Number.isFinite(width)&&Number.isFinite(height)&&width>0&&height>0))throw Error('图片尺寸无效');
 const w=width>=height?1216:684,h=width>=height?684:1216;
 if(wallpaper){
  const scale=w/width,sh=Math.min(height,h/scale),dh=Math.min(h,height*scale);
  return {w,h,sx:0,sy:(height-sh)/2,sw:width,sh,dx:0,dy:(h-dh)/2,dw:w,dh};
 }
 const scale=Math.max(w/width,h/height),sw=w/scale,sh=h/scale;
 return {w,h,sx:(width-sw)/2,sy:(height-sh)/2,sw,sh,dx:0,dy:0,dw:w,dh:h};
}
async function thingsos_image_decode(file){
 if(typeof createImageBitmap==='function'){
  try{return await createImageBitmap(file,{imageOrientation:'from-image'})}catch{}
 }
 const url=URL.createObjectURL(file);
 try{return await new Promise((resolve,reject)=>{const img=new Image();img.onload=()=>resolve(img);img.onerror=()=>reject(Error('图片无法解码，请选择有效的 PNG/JPG/WebP'));img.src=url})}
 finally{URL.revokeObjectURL(url)}
}
function thingsos_png_chunk(type,data){
 const out=new Uint8Array(data.length+12),view=new DataView(out.buffer);view.setUint32(0,data.length);
 for(let i=0;i<4;i++)out[4+i]=type.charCodeAt(i);out.set(data,8);
 let crc=0xffffffff;for(let i=4;i<out.length-4;i++){crc^=out[i];for(let b=0;b<8;b++)crc=(crc>>>1)^((crc&1)?0xedb88320:0)}
 view.setUint32(out.length-4,(crc^0xffffffff)>>>0);return out;
}
async function thingsos_gray_png(rgba,w,h){
 const scan=new Uint8Array((w+1)*h);
 for(let y=0;y<h;y++)for(let x=0;x<w;x++)scan[y*(w+1)+x+1]=rgba[(y*w+x)*4];
 const stream=new Blob([scan]).stream().pipeThrough(new CompressionStream('deflate'));
 const compressed=new Uint8Array(await new Response(stream).arrayBuffer()),header=new Uint8Array(13),view=new DataView(header.buffer);
 view.setUint32(0,w);view.setUint32(4,h);header[8]=8; // one grayscale channel, no alpha
 return new Blob([new Uint8Array([137,80,78,71,13,10,26,10]),thingsos_png_chunk('IHDR',header),thingsos_png_chunk('IDAT',compressed),thingsos_png_chunk('IEND',new Uint8Array())],{type:'image/png'});
}
async function thingsos_prepare_image(file,wallpaper=false){
 if(!/\.(png|jpe?g|webp)$/i.test(file.name))return {file,info:''};
 if(file.size>64*1024*1024)throw Error('图片过大，请选择小于 64 MB 的图片');
 let image,canvas;
 try{
  image=await thingsos_image_decode(file);
  const crop=thingsos_image_crop(image.width||image.naturalWidth,image.height||image.naturalHeight,wallpaper);
  canvas=document.createElement('canvas');canvas.width=crop.w;canvas.height=crop.h;
  const context=canvas.getContext('2d',{willReadFrequently:true});if(!context)throw Error('浏览器无法处理图片');
  context.fillStyle='#fff';context.fillRect(0,0,crop.w,crop.h);context.imageSmoothingEnabled=true;context.imageSmoothingQuality='high';
  context.drawImage(image,crop.sx,crop.sy,crop.sw,crop.sh,crop.dx,crop.dy,crop.dw,crop.dh);
  const pixels=context.getImageData(0,0,crop.w,crop.h);
  for(let i=0;i<pixels.data.length;i+=4){const alpha=pixels.data[i+3]/255,gray=Math.round((pixels.data[i]*299+pixels.data[i+1]*587+pixels.data[i+2]*114)/1000*alpha+255*(1-alpha));pixels.data[i]=pixels.data[i+1]=pixels.data[i+2]=gray;pixels.data[i+3]=255}
  let blob,ext;
  if(/\.png$/i.test(file.name)&&typeof CompressionStream==='function'){
   blob=await thingsos_gray_png(pixels.data,crop.w,crop.h);ext='.png';
  }else{
   context.putImageData(pixels,0,0);ext='.jpg';
   for(const quality of [0.86,0.78,0.68]){blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/jpeg',quality));if(blob&&blob.size<=768*1024)break}
  }
  if(!blob||blob.size>1024*1024)throw Error('图片压缩失败，请换一张图片重试');
  const name=file.name.replace(/\.(png|jpe?g|webp)$/i,'')+ext;
  return {file:new File([blob],name,{type:blob.type,lastModified:Date.now()}),info:`${name} · ${crop.w}×${crop.h} · ${Math.ceil(blob.size/1024)} KB · ${wallpaper?(crop.w>crop.h?"灰度横屏适配 · 透明补白":"灰度完整宽度 · 透明补白"):"灰度居中裁剪"}`};
 }finally{
  image?.close?.();if(canvas){canvas.width=1;canvas.height=1}
 }
}
