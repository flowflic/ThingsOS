// SPDX-License-Identifier: Apache-2.0
// Local HTTP fallback for browsers without SubtleCrypto. No remote scripts.
function thingsos_sha256(input) {
  const bytes = new Uint8Array(input), size = Math.ceil((bytes.length + 9) / 64) * 64;
  const padded = new Uint8Array(size); padded.set(bytes); padded[bytes.length] = 128;
  const view = new DataView(padded.buffer), bits = bytes.length * 8;
  view.setUint32(size - 8, Math.floor(bits / 4294967296)); view.setUint32(size - 4, bits >>> 0);
  const k = new Uint32Array(64), state = new Uint32Array(8); let count = 0;
  for (let prime = 2; count < 64; prime++) {
    let isPrime = true;
    for (let divisor = 2; divisor * divisor <= prime; divisor++) if (prime % divisor === 0) { isPrime = false; break; }
    if (!isPrime) continue;
    const cube = Math.cbrt(prime); k[count] = Math.floor((cube - Math.floor(cube)) * 4294967296);
    if (count < 8) { const root = Math.sqrt(prime); state[count] = Math.floor((root - Math.floor(root)) * 4294967296); }
    count++;
  }
  const rotate = (v, n) => (v >>> n) | (v << (32 - n)), words = new Uint32Array(64);
  for (let block = 0; block < size; block += 64) {
    for (let i = 0; i < 16; i++) words[i] = view.getUint32(block + i * 4);
    for (let i = 16; i < 64; i++) {
      const x = words[i - 15], y = words[i - 2];
      words[i] = (words[i - 16] + (rotate(x,7)^rotate(x,18)^(x>>>3)) + words[i - 7] + (rotate(y,17)^rotate(y,19)^(y>>>10))) >>> 0;
    }
    let [a,b,c,d,e,f,g,h] = state;
    for (let i = 0; i < 64; i++) {
      const t1 = (h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + k[i] + words[i]) >>> 0;
      const t2 = ((rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c))) >>> 0;
      h=g; g=f; f=e; e=(d+t1)>>>0; d=c; c=b; b=a; a=(t1+t2)>>>0;
    }
    const result = [a,b,c,d,e,f,g,h]; for (let i = 0; i < 8; i++) state[i] = (state[i] + result[i]) >>> 0;
  }
  return Array.from(state, v => v.toString(16).padStart(8,'0')).join('');
}
/* Preflight only; ESP-IDF verifies the complete image again before activation. */
function thingsos_firmware_header(data,size) {
 const bytes=new Uint8Array(data);
 if(size<288 || size>0xae0000 || bytes.length<288) throw Error('应用文件大小无效');
 const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
 if(bytes[0]!==0xe9 || view.getUint16(12,true)!==0x20) throw Error('固件不匹配 ESP32-S31');
 if(view.getUint32(32,true)!==0xabcd5432) throw Error('应用描述无效，不能使用完整 USB 包');
 function field(start){let text='';for(let i=start;i<start+32 && bytes[i];i++)text+=String.fromCharCode(bytes[i]);return text}
 const project=field(80),version=field(48);
 if(project!=='ThingsOS_native') throw Error('项目不匹配 ThingsOS_native');
 if(!version) throw Error('固件版本为空');
 const elf=Array.from(bytes.slice(176,208),n=>n.toString(16).padStart(2,"0")).join("");
 return {project,version,elf};
}
