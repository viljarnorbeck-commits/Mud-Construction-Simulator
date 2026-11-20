import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {inflateSync,crc32} from 'node:zlib';
import {landscape} from '../tools/earthworks-terrain.mjs';
const W=JSON.parse(readFileSync('world/42/world.json'));
const png=readFileSync('world/42/height.png'),parts=[];
for(let p=8;p<png.length;) {
  const n=png.readUInt32BE(p),type=png.toString('ascii',p+4,p+8);
  assert.equal(crc32(png.subarray(p+4,p+8+n)),png.readUInt32BE(p+8+n));
  if(type==='IDAT')parts.push(png.subarray(p+8,p+8+n));p+=n+12;
}
const size=png.readUInt32BE(16),rows=inflateSync(Buffer.concat(parts)),map=W.heightmap;
assert.equal(size,2049);assert.equal(png[24],16);
const h=(x,y)=>map.minElevation+rows.readUInt16BE(Math.round(y)*(size*2+1)+1+Math.round(x)*2)/65535*(map.maxElevation-map.minElevation);
let steep=0,easy=0,rough=0,min=Infinity,max=-Infinity;
for(let y=2;y<size-2;y++)for(let x=2;x<size-2;x++) {
  const z=h(x,y),grade=Math.hypot(h(x+1,y)-h(x-1,y),h(x,y+1)-h(x,y-1))*.5;
  min=Math.min(min,z);max=Math.max(max,z);if(grade>.7)steep++;if(grade<.15)easy++;
  if(Math.abs(h(x+2,y)+h(x-2,y)-2*z)>.5)rough++;
}
assert(max-min>180);assert(steep/size**2>.12);assert(easy/size**2>.08);
assert(rough/size**2>.005,'Keep eroded slope breaks rather than a uniformly blurred world');
for(const p of W.buildSites)for(let y=-10;y<=10;y+=5)for(let x=-10;x<=10;x+=5)assert(Math.abs(h(p.x+x,p.y+y)-p.z)<.006);
const L=landscape(W.seed),B=landscape(W.seed),C=landscape(W.seed+1);
assert.equal(L.at(623,917).z,B.at(623,917).z);assert.notEqual(L.at(623,917).z,C.at(623,917).z);
const geology=(x,y)=>W.geology.filter(p=>((x-p.x)/p.rx)**2+((y-p.y)/p.ry)**2<1).at(-1);
for(const y of [1510,1600,1640,1730,1810]) {
  const bank=236+7*Math.sin((y-1640)/49),ravine=337+13*Math.sin((y-1640)/62);
  const grade=(h(bank+3,y)-h(bank,y))/3;
  assert(grade>.35&&grade<.85,'A difficult but coherent, nonvertical till slope');
  assert(Math.min(h(ravine-27,y),h(ravine+27,y))-h(ravine,y)>7,'Incised ravine retains depth and excavatable sides');
  assert(geology(bank,y).soilDepth>=25);assert(geology(bank,y).material!=='crushed-rock');
}
for(const p of W.river.centerline)assert(p.z-h(p.x,p.y)>1.45);
for(const p of W.creek.centerline.filter(p=>p.x>205))assert(p.z-h(p.x,p.y)>.75);
for(const key of ['river','creek'])for(let i=1;i<W[key].centerline.length;i++)assert(W[key].centerline[i].z<W[key].centerline[i-1].z);
assert(W.challenges.length>=8);assert(W.challenges.filter(p=>Math.hypot(p.x-W.start.x,p.y-W.start.y)<320).length>=3);
// A deliberately generous truck climb limit still cannot reach the delivery site
// by simply detouring around the nearby bank. Search the entire world, not one lane.
const step=4,n=513,seen=new Uint8Array(n*n),queue=new Int32Array(n*n);
const cell=(x,y)=>Math.round(y/step)*n+Math.round(x/step);
let head=0,tail=1;queue[0]=cell(W.start.x,W.start.y);seen[queue[0]]=1;
const destination=cell(W.goal.x,W.goal.y);
while(head<tail&&!seen[destination]) {
  const id=queue[head++],x=id%n,y=Math.floor(id/n),z=h(x*step,y*step);
  for(const [dx,dy] of [[1,0],[-1,0],[0,1],[0,-1],[1,1],[1,-1],[-1,1],[-1,-1]]) {
    const nx=x+dx,ny=y+dy;if(nx<0||ny<0||nx>=n||ny>=n)continue;
    const next=ny*n+nx;if(seen[next])continue;
    if((h(nx*step,ny*step)-z)/(step*Math.hypot(dx,dy))>.35)continue;
    // A truck cannot zigzag up a scarp by ignoring its lateral rollover slope.
    // Match the production wheeled roll limit at 80% payload: .80 - .23*.80.
    const px=nx*step,py=ny*step;
    if(px>0&&py>0&&px<2048&&py<2048) {
      const gx=(h(px+1,py)-h(px-1,py))*.5,gy=(h(px,py+1)-h(px,py-1))*.5;
      if(Math.abs((-dy*gx+dx*gy)/Math.hypot(dx,dy))>.616)continue;
    }
    seen[next]=1;queue[tail++]=next;
  }
}
assert(!seen[destination],'Unworked delivery terrace must require an actual cut or fill, even after a world-wide detour');
assert(W.piles.filter(p=>Math.hypot(p.x-W.start.x,p.y-W.start.y)<150).some(p=>p.material==='gravel'&&p.volumeM3>1500));
assert(W.trees.length>5000);assert(W.boulders.length>500);
for(const t of W.trees)assert(Math.abs(h(t.x,t.y)-t.z)<.0001);
// The checked-in physical fixture must describe the actual encoded world, not an easier test track.
for(const line of readFileSync('world/42/acceptance-profile.csv','utf8').trim().split('\n').slice(1)) {
  const [x,y,z]=line.split(',').map(Number);assert(Math.abs(h(x,y)-z)<1e-8);
}
let nearVertical=0,rockSteep=0,soilSteep=0,maxSoil=0;
for(let y=1;y<size-1;y+=2)for(let x=1;x<size-1;x+=2) {
  const rise=Math.max(Math.abs(h(x+1,y)-h(x,y)),Math.abs(h(x,y+1)-h(x,y)));
  if(rise>4)nearVertical++;
  if(geology(x,y)?.material==='crushed-rock'){if(rise>1.2)rockSteep++;}
  else {maxSoil=Math.max(maxSoil,rise);if(rise>.35)soilSteep++;assert(rise<.84,'Soil cannot become an arbitrary permanent wall');}
}
assert.equal(nearVertical,0);assert(rockSteep>100);assert(soilSteep>10000);
assert.equal(W.worldId,`earthworks-v4-${W.seed}`);assert.equal(W.roadContract,false);
assert.equal(W.generation.conditioning.passes>0,true);
console.log(JSON.stringify({passed:true,range:[min,max],steepPercent:steep/size**2*100,roughPercent:rough/size**2*100,easyPercent:easy/size**2*100,nearVertical,rockSteep,maxSoil,trees:W.trees.length,boulders:W.boulders.length}));
