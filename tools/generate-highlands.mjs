import {mkdirSync,writeFileSync} from 'node:fs';
import {join} from 'node:path';
import {deflateSync,crc32} from 'node:zlib';
const earthworks=process.argv.includes('--earthworks');
const {landscape,SIZE,MIN_Z,MAX_Z,smooth}=await import(earthworks?'./earthworks-terrain.mjs':'./highlands-terrain.mjs');
const seed=Number(process.argv[2]??42), output=process.argv[3]??`world/${earthworks?'earthworks':'highlands'}-${seed}`;
if(!Number.isInteger(seed)||seed<0||seed>0xffffffff) throw Error('Invalid seed');
const begin=performance.now(), L=landscape(seed), heights=new Float32Array(SIZE*SIZE);
const heightRows=Buffer.alloc(SIZE*(SIZE*2+1)), overview=Buffer.alloc(SIZE*(SIZE*3+1));
let low=Infinity,high=-Infinity,steep=0,gentle=0;
for(let y=0;y<SIZE;y++) for(let x=0;x<SIZE;x++) {
  const t=L.at(x,y),i=y*SIZE+x;
  if(t.z<MIN_Z||t.z>MAX_Z) throw Error(`Height outside encoding range ${t.z}`);
  const q=Math.round((t.z-MIN_Z)/(MAX_Z-MIN_Z)*65535);
  heights[i]=MIN_Z+q/65535*(MAX_Z-MIN_Z);
  heightRows.writeUInt16BE(q,y*(SIZE*2+1)+1+x*2);
  low=Math.min(low,t.z); high=Math.max(high,t.z);
  const slope=x&&y?Math.hypot(heights[i]-heights[i-1],heights[i]-heights[i-SIZE]):0;
  if(slope>.7)steep++; if(slope<.15)gentle++;
  const shade=Math.max(-33,Math.min(33,x?(heights[i-1]-t.z)*26:0));
  let c=t.water?[46,100,122]:slope>.8?[116,122,119]:t.wet>.5?[95,75,52]:[72+t.z*.22,111+t.z*.12,66+t.z*.18];
  if(t.pile) { c=t.pile.material==='soil'?[149,104,68]:[174,166,141];
    t.pile.volumeM3+=t.pile.height*Math.max(0,1-Math.hypot(x-t.pile.x,y-t.pile.y)/t.pile.radius); }
  c.forEach((v,k)=>overview[y*(SIZE*3+1)+1+x*3+k]=Math.max(0,Math.min(255,Math.round(v+shade))));
}
const height=(x,y)=>heights[Math.max(0,Math.min(SIZE-1,Math.round(y)))*SIZE+Math.max(0,Math.min(SIZE-1,Math.round(x)))];
const trees=[],boulders=[];
for(let y=6;y<SIZE-6;y+=7)for(let x=6;x<SIZE-6;x+=7){
  const tx=x+Math.round(L.hash(x,y)*5)-2,ty=y+Math.round(L.hash(y,x)*5)-2;
  const z=height(tx,ty),slope=Math.hypot(height(tx+1,ty)-height(tx-1,ty),height(tx,ty+1)-height(tx,ty-1))*.5;
  const t=L.at(tx,ty),clear=L.pads.some(p=>Math.hypot(tx-p.x,ty-p.y)<p.flat+15)||L.piles.some(p=>Math.hypot(tx-p.x,ty-p.y)<p.radius+6);
  if(clear||t.water)continue;
  const forest=.04+.84*smooth((L.noise(tx/140+8,ty/160-2)-.35)/.35)*(1-smooth((z-155)/90));
  if(slope<.72&&t.wet<.85&&L.hash(x+91,y+17)<forest){
    const scale=.45+L.hash(x+150,y+94)*1.2;
    trees.push({x:tx,y:ty,z,scale:+scale.toFixed(3),diameter:+(.18+scale**1.7*.22).toFixed(3),height:+(5+scale*5).toFixed(3)});
    const pixel=ty*(SIZE*3+1)+1+tx*3; overview.set([30,61,40],pixel);
  }else if(slope>.28&&L.hash(x-91,y+32)>.90){
    boulders.push({x:tx,y:ty,z,radius:+(.55+L.hash(x+17,y-49)*1.5).toFixed(3)});
  }
}
const world={seed,worldId:`${earthworks?'earthworks-v3':'highlands-v2'}-${seed}`,dimensionsM:[SIZE-1,SIZE-1],
  heightmap:{file:'height.png',samples:SIZE,spacingM:1,minElevation:MIN_Z,maxElevation:MAX_Z,observedRangeM:[low,high]},
  start:L.start,goal:L.goal,roadContract:false,
  river:{centerline:Array.from({length:257},(_,i)=>({x:L.riverX(i*8),y:i*8,z:L.riverZ(i*8),width:L.riverWidth(i*8)})),
    fords:[820,1560].map(y=>({x:L.riverX(y),y,centerDepthM:1.6}))},
  creek:{centerline:Array.from({length:Math.floor((L.creekEnd-180)/4)+1},(_,i)=>L.creek(180+i*4))},
  mudPits:L.pits,buildSites:L.pads,geology:L.geology??[...L.pits.map(p=>({...p,material:'clay',soilDepth:3})),
    {x:1130,y:1650,rx:130,ry:140,material:'sand',soilDepth:5}],
  challenges:L.challenges??[{name:'Mountain switchback',x:1660,y:580},{name:'Muddy valley',x:780,y:1570},
    {name:'Forest passage',x:540,y:1100},{name:'Creek gorge',x:650,y:L.creek(650).y}],
  piles:L.piles.map(p=>({...p,volumeM3:Math.round(p.volumeM3)})),trees,boulders,
  generation:{seconds:(performance.now()-begin)/1000,steepFraction:steep/heights.length,gentleFraction:gentle/heights.length}};
function png(depth,type,rows){
  const chunk=(name,data)=>{const b=Buffer.alloc(data.length+12);b.writeUInt32BE(data.length);b.write(name,4);data.copy(b,8);b.writeUInt32BE(crc32(b.subarray(4,-4)),b.length-4);return b;};
  const h=Buffer.alloc(13);h.writeUInt32BE(SIZE);h.writeUInt32BE(SIZE,4);h[8]=depth;h[9]=type;
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',h),chunk('IDAT',deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]);
}
mkdirSync(output,{recursive:true});writeFileSync(join(output,'height.png'),png(16,0,heightRows));
writeFileSync(join(output,'overview.png'),png(8,2,overview));writeFileSync(join(output,'world.json'),JSON.stringify(world,null,2)+'\n');
if(earthworks) {
  // Quantized physical data consumed by the C++ earthwork/vehicle acceptance test.
  const rows=['x,y,height'];
  for(let x=190;x<=686;x++)for(let y=1635;y<=1645;y++) {
    const z=MIN_Z+heightRows.readUInt16BE(y*(SIZE*2+1)+1+x*2)/65535*(MAX_Z-MIN_Z);
    rows.push(`${x},${y},${z.toFixed(9)}`);
  }
  writeFileSync(join(output,'acceptance-profile.csv'),rows.join('\n')+'\n');
}
console.log(JSON.stringify({output,range:[low,high],trees:trees.length,boulders:boulders.length,...world.generation}));
