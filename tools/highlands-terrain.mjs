// Deterministic, domain-warped ridges and connected drainage at one-metre resolution.
export const SIZE = 2049;
export const MIN_Z = -32;
export const MAX_Z = 320;
export const smooth = t => { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); };
const mix = (a, b, t) => a + (b - a) * t;
const hill = (x, y, cx, cy, rx, ry) => Math.exp(-(((x - cx) / rx) ** 2) - ((y - cy) / ry) ** 2);

export function landscape(seed) {
  function hash(x, y) {
    let h = Math.imul(x, 374761393) ^ Math.imul(y, 668265263) ^ seed;
    h = Math.imul(h ^ (h >>> 13), 1274126177);
    return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
  }
  function noise(x, y) {
    const ix = Math.floor(x), iy = Math.floor(y), u = smooth(x - ix), v = smooth(y - iy);
    return mix(mix(hash(ix, iy), hash(ix + 1, iy), u), mix(hash(ix, iy + 1), hash(ix + 1, iy + 1), u), v);
  }
  const phase = hash(18, 42) * Math.PI * 2;
  const riverX = y => 1040 + 100 * Math.sin(y / 330 + phase) + 27 * Math.sin(y / 103);
  const riverZ = y => 48 - 0.018 * y;
  const riverWidth = y => 18 + 4 * Math.sin(y / 170 + phase);
  const start = {x: 155, y: 1640, z: 42};
  const goal = {x: 1810, y: 380, z: 174};
  const mouthY = 1330, creekEnd = Math.round(riverX(mouthY) / 4) * 4;
  function creek(x) {
    const distance = creekEnd - x;
    return {x, y: mouthY + 19 * Math.sin(distance / 110) + 8 * Math.sin(distance / 34),
      z: riverZ(mouthY) + distance * 0.022 + distance ** 2 * 0.00008,
      width: 5.5 + Math.sin(x / 35), depth: 0.9};
  }
  const pits = [{x: 780, y: 1570, rx: 100, ry: 55, depth: 2.8},
    {x: 1250, y: 1010, rx: 75, ry: 100, depth: 3.2}, {x: 1540, y: 480, rx: 78, ry: 40, depth: 2}];
  const pads = [{...start, flat: 29, edge: 66}, {...goal, flat: 24, edge: 80},
    {x: 540, y: 1750, z: 58, flat: 20, edge: 58}, {x: 1440, y: 1410, z: 68, flat: 28, edge: 75}];
  const piles = [{material:'gravel',x:250,y:1590,radius:19,height:6},
    {material:'soil',x:455,y:1720,radius:24,height:7}, {material:'gravel',x:835,y:1390,radius:22,height:8},
    {material:'sand',x:1130,y:1650,radius:23,height:6}, {material:'crushed-rock',x:1490,y:1250,radius:25,height:9},
    {material:'gravel',x:1620,y:580,radius:20,height:7}].map(p => ({...p, volumeM3:0}));
  function at(x, y, deposits = true, buildPads = true) {
    const wx = x + (noise(x / 250, y / 250) - 0.5) * 100;
    const wy = y + (noise(x / 300 + 7, y / 300 - 4) - 0.5) * 90;
    const distance = Math.abs(x - riverX(y));
    const ridges = 1 - Math.abs(noise(wx / 115, wy / 130) * 2 - 1);
    const west = hill(wx, wy, 430, 690, 260, 470);
    const east = hill(wx, wy, 1570, 830, 340, 370);
    let z = riverZ(y) + 7 + distance * 0.027 + west * 135 + east * 177;
    z += (west + east) * (ridges ** 3 * 22 + (noise(wx / 37, wy / 41) - 0.5) * 9);
    z += (noise(wx / 75, wy / 85) - 0.5) * 7;
    // A weathered escarpment creates genuine rock faces rather than isolated random cones.
    z += 47 * smooth((x - 1640 - 20 * Math.sin(y / 80)) / 19) * hill(x,y,1770,450,240,370);
    // Connected dry gullies descending toward the central valley.
    const gully = y - (760 + 0.22 * (x - 650) + 15 * Math.sin(x / 57));
    z -= 27 * Math.exp(-((gully / 18) ** 2)) * hill(x,y,610,750,290,240);
    const riverBlend = 1 - smooth((distance - riverWidth(y) * 0.5) / 25);
    const crossing = Math.max(...[820,1560].map(v => Math.exp(-(((y-v)/18)**2))));
    const channel = riverZ(y) - mix(3.5, 1.6, crossing);
    z = mix(z, channel, riverBlend);
    let wet = 0.1, water = distance < riverWidth(y) * 0.5 + 10 && z < riverZ(y);
    if (x >= 180 && x <= creekEnd) {
      const c = creek(x), d = Math.abs(y-c.y);
      const carve = (1-smooth((d-c.width*0.5)/26))*smooth((x-180)/22);
      z = mix(z, Math.min(z,c.z-c.depth), carve);
      wet = Math.max(wet, 0.85*(1-smooth((d-c.width*.5)/16)));
      water ||= d < c.width*.5+4 && z < c.z;
    }
    for (const p of pits) {
      const w = 1-smooth(Math.hypot((x-p.x)/p.rx,(y-p.y)/p.ry));
      z -= p.depth*w; wet=Math.max(wet,w);
    }
    if(buildPads)for (const p of pads) z=mix(p.z,z,smooth((Math.hypot(x-p.x,y-p.y)-p.flat)/(p.edge-p.flat)));
    let pile;
    if (deposits) for (const p of piles) {
      const thickness=p.height*Math.max(0,1-Math.hypot(x-p.x,y-p.y)/p.radius);
      if (thickness>0) {z+=thickness; pile=p; break;}
    }
    return {z,wet,water,pile};
  }
  for(const p of pads)p.z=at(p.x,p.y,false,false).z;
  start.z=pads[0].z;goal.z=pads[1].z;
  return {at,noise,hash,start,goal,pits,piles,pads,riverX,riverZ,riverWidth,creek,creekEnd};
}
