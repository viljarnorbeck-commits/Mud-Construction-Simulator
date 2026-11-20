// One metre physical columns; half-metre surface reconstruction is shared by rendering/collision.
// Folded sedimentary ridges, branching incision and cohesive earth scarps replace Gaussian hills.
import {landscape as drainage, SIZE, MIN_Z, MAX_Z, smooth} from './highlands-terrain.mjs';
export {SIZE, MIN_Z, MAX_Z, smooth};
const mix=(a,b,t)=>a+(b-a)*t;
const window=(t,a,b,fade)=>smooth((t-a)/fade)*smooth((b-t)/fade);
export function landscape(seed) {
  const D=drainage(seed), {noise,hash,riverX,riverZ,riverWidth,creek,creekEnd}=D;
  const bankX=y=>236+7*Math.sin((y-1640)/49);
  const ravineX=y=>337+13*Math.sin((y-1640)/62);
  const start={x:155,y:1640,z:0}, goal={x:565,y:1630,z:0};
  const pads=[{...start,flat:38,edge:55},{...goal,flat:18,edge:29},
    {x:1440,y:1410,z:0,flat:23,edge:39}];
  const pits=[{x:435,y:1540,rx:64,ry:57,depth:2.5},
    {x:780,y:1570,rx:100,ry:55,depth:3.8}, {x:1250,y:1010,rx:75,ry:100,depth:4.2},
    {x:1540,y:480,rx:78,ry:40,depth:3.5}];
  const piles=[{material:'gravel',x:118,y:1555,radius:18,height:7},
    {material:'soil',x:92,y:1725,radius:24,height:8},
    {material:'gravel',x:835,y:1390,radius:22,height:8},
    {material:'sand',x:1130,y:1650,radius:23,height:6},
    {material:'crushed-rock',x:1490,y:1250,radius:25,height:9},
    {material:'gravel',x:1620,y:580,radius:20,height:7}].map(p=>({...p,volumeM3:0}));
  // Thick glacial till remains excavatable even on a vertical-looking face. Explicit rock
  // outcrops are limited to the distant eastern massif, never inferred from slope.
  const geology=[{x:1024,y:1024,rx:1600,ry:1600,material:'soil',soilDepth:52},
    {x:260,y:1640,rx:76,ry:245,material:'clay',soilDepth:30},
    ...pits.map(p=>({...p,material:'clay',soilDepth:14})),
    {x:1130,y:1650,rx:130,ry:140,material:'sand',soilDepth:16},
    {x:1690,y:790,rx:110,ry:165,material:'crushed-rock',soilDepth:48}];
  const challenges=[
    {name:'Till escarpment',kind:'cut',x:bankX(1640),y:1640,approach:{x:210,y:1640},end:{x:282,y:1640}},
    {name:'Incised ravine',kind:'fill',x:ravineX(1665),y:1665,approach:{x:307,y:1665},end:{x:369,y:1665}},
    {name:'Saturated clay hollow',kind:'reinforce',x:435,y:1540},
    {name:'Northern ravine route',kind:'fill',x:ravineX(1510),y:1510},
    {name:'Western folded ridge',kind:'cut',x:560,y:920},
    {name:'Creek gorge',kind:'fill',x:650,y:creek(650).y},
    {name:'Eastern narrow spur',kind:'cut',x:1510,y:700},
    {name:'Valley bog',kind:'reinforce',x:1250,y:1010}];
  challenges.push({name:'Eroded delivery terrace',kind:'cut',x:565,y:1630});
  function at(x,y,deposits=true,buildPads=true) {
    const wx=x+(noise(x/220,y/230)-.5)*128;
    const wy=y+(noise(x/260+7,y/250-4)-.5)*105;
    const d=Math.abs(x-riverX(y));
    // Tectonic folds with fractal crest modulation; valleys retain low, open floodplains.
    const upland=smooth((d-65)/300);
    let amplitude=1, frequency=1, ridges=0, weight=1;
    for(let octave=0;octave<4;octave++) {
      const ridge=1-Math.abs(noise(wx/235*frequency+19,wy/330*frequency-7)*2-1);
      const signal=ridge*ridge*weight;
      ridges+=signal*amplitude;weight=Math.min(1,signal*1.8);
      frequency*=2.17;amplitude*=.46;
    }
    const folds=Math.pow(Math.abs(Math.sin(wx/112+wy/310+(noise(wx/170,wy/190)-.5)*2.1)),1.7);
    const mountain=window(y,70,1330,210);
    let z=riverZ(y)+8+d*.023+upland*mountain*(40+104*folds+58*ridges);
    z+=upland*(noise(wx/49,wy/53)-.5)*9+upland*mountain*(noise(wx/13,wy/17)-.5)*3.2;
    // Tributary gullies merge into the valley, with secondary erosion fingers on their banks.
    for(const [head,side] of [[430,-1],[840,1],[1150,-1]]) {
      const downstream=head+.16*d+18*Math.sin(x/71+head);
      const g=y-downstream, strength=window(d,65,870,90);
      const width=4+d*.012;
      z-=(13+16*upland)*Math.exp(-((g/width)**2))*strength;
      const fingers=Math.abs(Math.sin((wx+wy*.34)/31));
      z-=6*Math.exp(-(((g-side*23)/24)**2))*(1-fingers)**3*strength;
    }
    // A continuous sediment terrace and incised drainage immediately outside the machinery yard.
    const local=window(y,1390,1900,65);
    const bank=x-bankX(y);
    z+=18*smooth(bank/4)*(1-smooth((bank-29)/95))*local;
    const ravine=Math.abs(x-ravineX(y));
    z-=15*(1-smooth((ravine-3)/7))*local;
    // An isolated remnant of the sediment terrace carries the delivery site. Its
    // continuous eroded margin prevents a flat detour from bypassing all earthwork.
    // Players can cut a ramp anywhere around it or build an embankment from below.
    const remnant=Math.hypot((x-goal.x)/90,(y-goal.y)/120)+
      (noise(x/37+21,y/43-17)-.5)*.07;
    z+=11*(1-smooth((remnant-1)*30));
    // Additional weathered terrace edges across the map offer alternative cut/fill corridors.
    z+=21*smooth((x-1490-24*Math.sin(y/76))/5)*window(x,1430,1820,100)*window(y,320,1160,90);
    const riverBlend=1-smooth((d-riverWidth(y)*.5)/13);
    const crossing=Math.max(...[820,1560].map(v=>Math.exp(-(((y-v)/18)**2))));
    z=mix(z,riverZ(y)-mix(4.5,1.6,crossing),riverBlend);
    let wet=.1,water=d<riverWidth(y)*.5+10&&z<riverZ(y);
    if(x>=180&&x<=creekEnd) {
      const c=creek(x),dc=Math.abs(y-c.y);
      const carve=(1-smooth((dc-c.width*.5)/9))*smooth((x-180)/22);
      z=mix(z,Math.min(z,c.z-c.depth),carve);
      wet=Math.max(wet,.85*(1-smooth((dc-c.width*.5)/16)));
      water ||= dc<c.width*.5+4&&z<c.z;
    }
    for(const p of pits) {
      const w=1-smooth(Math.hypot((x-p.x)/p.rx,(y-p.y)/p.ry));z-=p.depth*w;wet=Math.max(wet,w);
    }
    if(buildPads)for(const p of pads)z=mix(p.z,z,smooth((Math.hypot(x-p.x,y-p.y)-p.flat)/(p.edge-p.flat)));
    let pile;
    if(deposits)for(const p of piles) {
      const thickness=p.height*Math.max(0,1-Math.hypot(x-p.x,y-p.y)/p.radius);
      if(thickness>0){z+=thickness;pile=p;break;}
    }
    return {z,wet,water,pile};
  }
  for(const p of pads)p.z=at(p.x,p.y,false,false).z;
  start.z=pads[0].z;goal.z=pads[1].z;
  return {at,noise,hash,start,goal,pits,piles,pads,riverX,riverZ,riverWidth,creek,creekEnd,geology,challenges};
}
