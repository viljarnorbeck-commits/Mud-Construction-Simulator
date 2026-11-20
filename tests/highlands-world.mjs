import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {inflateSync,crc32} from 'node:zlib';
import {join} from 'node:path';
import {landscape} from '../tools/highlands-terrain.mjs';
const folder=process.argv[2]??'world/highlands-42', W=JSON.parse(readFileSync(join(folder,'world.json')));
const png=readFileSync(join(folder,'height.png')),parts=[];
for(let p=8;p<png.length;){const n=png.readUInt32BE(p),type=png.toString('ascii',p+4,p+8);
  assert.equal(crc32(png.subarray(p+4,p+8+n)),png.readUInt32BE(p+8+n));
  if(type==='IDAT')parts.push(png.subarray(p+8,p+8+n));p+=n+12;}
const size=png.readUInt32BE(16),rows=inflateSync(Buffer.concat(parts)),map=W.heightmap;
assert.equal(size,2049);assert.equal(size,map.samples);assert.equal(png[24],16);
const h=(x,y)=>map.minElevation+rows.readUInt16BE(Math.round(y)*(size*2+1)+1+Math.round(x)*2)/65535*(map.maxElevation-map.minElevation);
let min=Infinity,max=-Infinity,steep=0,easy=0;
for(let y=1;y<size-1;y++)for(let x=1;x<size-1;x++){
  const z=h(x,y),grade=Math.hypot(h(x+1,y)-h(x-1,y),h(x,y+1)-h(x,y-1))*.5;
  min=Math.min(min,z);max=Math.max(max,z);if(grade>.7)steep++;if(grade<.15)easy++;
}
assert(max-min>220);assert(steep/size**2>.02);assert(easy/size**2>.35);
for(const p of W.buildSites)for(let y=-10;y<=10;y+=5)for(let x=-10;x<=10;x+=5)assert(Math.abs(h(p.x+x,p.y+y)-p.z)<.006);
for(const p of W.river.centerline)assert(p.z-h(p.x,p.y)>1.45,'River must be a real depression');
for(let i=1;i<W.river.centerline.length;i++)assert(W.river.centerline[i].z<W.river.centerline[i-1].z);
for(let i=1;i<W.creek.centerline.length;i++)assert(W.creek.centerline[i].z<W.creek.centerline[i-1].z);
for(const p of W.creek.centerline.filter(p=>p.x>205))assert(p.z-h(p.x,p.y)>.75,'Creek must fit terrain');
assert(W.trees.length>10000);assert(W.boulders.length>500);assert.equal(new Set(W.trees.map(t=>`${t.x},${t.y}`)).size,W.trees.length);
for(const t of W.trees){
  assert(Math.abs(h(t.x,t.y)-t.z)<.0001);
  const grade=Math.hypot(h(t.x+1,t.y)-h(t.x-1,t.y),h(t.x,t.y+1)-h(t.x,t.y-1))*.5;
  assert(grade<.73,'Trees must not grow on cliffs');assert(t.diameter>.1&&t.height>4);
}
const A=landscape(W.seed),B=landscape(W.seed),C=landscape(W.seed+1);
assert.equal(A.at(623,917).z,B.at(623,917).z);assert.notEqual(A.at(623,917).z,C.at(623,917).z);
assert.equal(W.worldId,`highlands-v2-${W.seed}`);assert.equal(W.roadContract,false);
console.log(JSON.stringify({passed:true,range:[min,max],steepPercent:steep/size**2*100,easyPercent:easy/size**2*100,trees:W.trees.length,boulders:W.boulders.length}));
