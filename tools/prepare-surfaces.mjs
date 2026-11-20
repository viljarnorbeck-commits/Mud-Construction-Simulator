// Deterministic, dependency-free preparation of the CC0 scans. Originals stay intact.
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
const folder=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../art/surfaces');
const target=path.join(folder,'Prepared'); fs.mkdirSync(target,{recursive:true});
const names=['Soil','Mud','Grass','Gravel','Sand','Clay','Rock'];
const latest=Math.max(fs.statSync(fileURLToPath(import.meta.url)).mtimeMs,...names.slice(0,4).map(n=>fs.statSync(path.join(folder,n+'_Color.png')).mtimeMs));
if(names.every(n=>fs.existsSync(path.join(target,n+'_Color.png'))&&fs.statSync(path.join(target,n+'_Color.png')).mtimeMs>=latest)){
 console.log('Prepared surface maps are current.');process.exit(0);
}
const crcTable=Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
function crc(bytes){let c=0xffffffff;for(const b of bytes)c=crcTable[(c^b)&255]^(c>>>8);return (c^0xffffffff)>>>0;}
function chunk(name,data){const type=Buffer.from(name),result=Buffer.alloc(data.length+12);result.writeUInt32BE(data.length);type.copy(result,4);data.copy(result,8);result.writeUInt32BE(crc(Buffer.concat([type,data])),data.length+8);return result;}
function read(name){
 const bytes=fs.readFileSync(path.join(folder,name+'_Color.png'));const w=bytes.readUInt32BE(16),h=bytes.readUInt32BE(20);
 if(bytes[24]!==8||bytes[25]!==6||bytes[28]!==0)throw Error('Expected non-interlaced RGBA8 PNG');
 let parts=[];for(let p=8;p<bytes.length;){const n=bytes.readUInt32BE(p);if(bytes.toString('ascii',p+4,p+8)==='IDAT')parts.push(bytes.subarray(p+8,p+8+n));p+=n+12;}
 const raw=zlib.inflateSync(Buffer.concat(parts)),out=Buffer.alloc(w*h*4),stride=w*4;
 const paeth=(a,b,c)=>{const p=a+b-c,da=Math.abs(p-a),db=Math.abs(p-b),dc=Math.abs(p-c);return da<=db&&da<=dc?a:db<=dc?b:c;};
 for(let y=0;y<h;y++){const filter=raw[y*(stride+1)];for(let x=0;x<stride;x++){const i=y*stride+x,a=x>=4?out[i-4]:0,b=y?out[i-stride]:0,c=y&&x>=4?out[i-stride-4]:0;
  if(filter>4)throw Error('Unknown PNG filter');out[i]=(raw[y*(stride+1)+1+x]+[0,a,b,(a+b)>>>1,paeth(a,b,c)][filter])&255;}}
 return {w,h,pixels:out};
}
function blur(image,radius){
 const {w,h,pixels}=image,tmp=new Float32Array(pixels.length),out=new Uint8Array(pixels.length),n=radius*2+1;
 // Wrap at the scan edges so mipmapping remains tileable.
 for(let y=0;y<h;y++)for(let c=0;c<4;c++){let sum=0;for(let x=-radius;x<=radius;x++)sum+=pixels[(y*w+(x+w)%w)*4+c];
  for(let x=0;x<w;x++){tmp[(y*w+x)*4+c]=sum/n;sum+=pixels[(y*w+(x+radius+1)%w)*4+c]-pixels[(y*w+(x-radius+w)%w)*4+c];}}
 for(let x=0;x<w;x++)for(let c=0;c<4;c++){let sum=0;for(let y=-radius;y<=radius;y++)sum+=tmp[(((y+h)%h)*w+x)*4+c];
  for(let y=0;y<h;y++){out[(y*w+x)*4+c]=Math.round(sum/n);sum+=tmp[(((y+radius+1)%h)*w+x)*4+c]-tmp[(((y-radius+h)%h)*w+x)*4+c];}}
 return out;
}
function write(name,w,h,pixels){const raw=Buffer.alloc(h*(w*4+1));for(let y=0;y<h;y++){raw[y*(w*4+1)]=0;Buffer.from(pixels.buffer,pixels.byteOffset+y*w*4,w*4).copy(raw,y*(w*4+1)+1);}
 const header=Buffer.alloc(13);header.writeUInt32BE(w);header.writeUInt32BE(h,4);header[8]=8;header[9]=6;
 fs.writeFileSync(path.join(target,name+'_Color.png'),Buffer.concat([Buffer.from('89504e470d0a1a0a','hex'),chunk('IHDR',header),chunk('IDAT',zlib.deflateSync(raw,{level:7})),chunk('IEND',Buffer.alloc(0))]));
}
const scans={};
for(const [name,source,radius,detail,contrast,tint,roughness] of [
 ['Soil','Soil',2,.38,.77,[1.02,.98,.94],.84],['Mud','Mud',3,.28,.72,[1,1,1],.65],
 ['Grass','Grass',1,.65,.86,[1,1,1],.90],['Gravel','Gravel',1,.65,.84,[1.03,1.03,1.02],.82],
 ['Sand','Soil',15,.035,.30,[1.28,1.20,1.00],.91],['Clay','Mud',7,.10,.45,[1.07,.92,.83],.82],
 ['Rock','Gravel',2,.45,.82,[.84,.88,.94],.79]]){
 const image=scans[source]??=read(source),soft=blur(image,radius),out=new Uint8Array(image.pixels.length);
 for(let i=0;i<out.length;i+=4){for(let c=0;c<3;c++){let value=soft[i+c]+(image.pixels[i+c]-soft[i+c])*detail;
   value=(value-128)*contrast+128;out[i+c]=Math.max(0,Math.min(255,Math.round(value*tint[c])));}
  out[i+3]=Math.round(255*Math.max(.58,Math.min(.96,roughness+(soft[i+3]/255-.65)*.12)));}
 write(name,image.w,image.h,out);console.log('Prepared '+name+' (2048px, filtered albedo + bounded roughness)');
}
