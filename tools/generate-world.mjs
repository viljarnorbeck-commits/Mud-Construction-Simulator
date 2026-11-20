import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { deflateSync, crc32 } from 'node:zlib';

// The normal command now generates the same natural earthworks world used by
// Windows builds. Keep the old 1 km map available only by explicit request.
if(!process.argv.includes('--legacy')) {
  process.argv[3]??=join('world',String(process.argv[2]??42));
  process.argv.push('--earthworks');
  await import('./generate-highlands.mjs');
  process.exit(0);
}
process.argv=process.argv.filter(arg=>arg!=='--legacy');

const seed = Number(process.argv[2] ?? 42);
if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff) {
  throw new Error('The seed must be an integer from 0 to 4294967295.');
}
const output = process.argv[3] ?? join('world', String(seed));
const size = 1025;
const minElevation = -32;
const maxElevation = 96;
const start = { x: 115, y: 795 };
const goal = { x: 906, y: 231 };
const fords = [345, 733];
const fordDepths = [1.52, 1.24];
const mudPits = [
  { x: 381, y: 735, rx: 67, ry: 36, depth: 2.4 },
  { x: 663, y: 384, rx: 48, ry: 36, depth: 2 },
  { x: 764, y: 662, rx: 43, ry: 54, depth: 2.8 },
];
const piles = [
  { material: 'soil', x: 174, y: 707, radius: 21, height: 7 },
  { material: 'gravel', x: 286, y: 538, radius: 19, height: 8 },
  { material: 'gravel', x: 581, y: 558, radius: 18, height: 7 },
  { material: 'soil', x: 697, y: 768, radius: 21, height: 7 },
  { material: 'crushed-rock', x: 700, y: 248, radius: 21, height: 9 },
].map(pile => ({ ...pile, volumeM3: 0 }));

let randomState = seed;
function random() {
  randomState = (1664525 * randomState + 1013904223) >>> 0;
  return randomState / 2 ** 32;
}

const phase = random() * 2 * Math.PI;
const ridgeOffset = (random() - 0.5) * 60;
const smooth = (value) => {
  const t = Math.max(0, Math.min(1, value));
  return t * t * (3 - 2 * t);
};
const mix = (a, b, weight) => a + (b - a) * weight;
const riverX = y => 505 + 34 * Math.sin(y / 116 + phase) + 9 * Math.sin(y / 37);
const waterLevel = y => 11 - 0.004 * y;
const riverWidth = y => 13 + 4 * Math.sin(y / 101 + phase);
const roadFord = fords[1];
const roadPits = [-1, 1].map(side => ({
  x: +((riverX(roadFord) + side * 17).toFixed(2)), y: roadFord, rx: 15, ry: 16, depth: 0.68,
}));
const roadGravel = { material: 'gravel', x: Math.round(riverX(roadFord)) - 76,
  y: roadFord - 28, radius: 13, height: 3.5, volumeM3: 0 };
piles.push(roadGravel);
const creekStart = 200;
const creekMouthY = 642;
const creekEnd = Math.round(riverX(creekMouthY) / 4) * 4;
const creekCrossingX = 320;

function creekAt(x) {
  const upstream = creekEnd - x;
  const t = (x - creekStart) / (creekEnd - creekStart);
  const y = creekMouthY + 10 * Math.sin(upstream / 33) + 10 * Math.sin(upstream / 85);
  const z = waterLevel(creekMouthY) + 6.8 * (1 - t) + 2.8 * Math.max(0, 1 - t / 0.28) ** 2
    - 0.065 * (1 - smooth(upstream / 10));
  const ford = Math.exp(-((x - creekCrossingX) ** 2) / 95);
  return { x, y, z, width: 4.8 + 1.15 * Math.sin(x / 21), depth: mix(0.76, 0.33, ford) };
}

const creekColumns = Array.from({ length: creekEnd - creekStart + 1 }, (_, i) => creekAt(creekStart + i));

function hill(x, y, cx, cy, rx, ry, height) {
  return height * Math.exp(-(((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2));
}

function levelPad(elevation, x, y, pad, flatRadius, edgeRadius, level) {
  const distance = Math.hypot(x - pad.x, y - pad.y);
  return mix(level, elevation, smooth((distance - flatRadius) / (edgeRadius - flatRadius)));
}

function terrainAt(x, y, includeWorksites = true) {
  const distanceFromRiver = Math.abs(x - riverX(y));
  const riverWeight = smooth((distanceFromRiver - riverWidth(y) / 2) / 24);
  const fordCut = Math.max(...fords.map((ford, i) =>
    (2.9 - (includeWorksites ? fordDepths[i] : 0.95)) * Math.exp(-((y - ford) ** 2) / 170)));
  const riverbed = waterLevel(y) - 2.9 + fordCut;
  const upland =
    waterLevel(y) + 3 + distanceFromRiver * 0.019
    + 2.4 * Math.sin(x / 52 + phase) * Math.cos(y / 63)
    + 1.4 * Math.sin((x + y) / 23) * Math.sin(y / 38 + phase)
    + hill(x, y, 260, 295 + ridgeOffset, 103, 133, 40)
    + hill(x, y, 390, 855, 75, 90, 33)
    + hill(x, y, 778, 535 + ridgeOffset, 140, 61, 58)
    + hill(x, y, 898, 224, 106, 123, 30);
  let elevation = mix(riverbed, upland, riverWeight);
  let mud = 0;
  for (const pit of mudPits) {
    const radius = Math.hypot((x - pit.x) / pit.rx, (y - pit.y) / pit.ry);
    const weight = 1 - smooth(radius);
    elevation -= pit.depth * weight;
    mud = Math.max(mud, weight);
  }
  if (includeWorksites) {
    for (const pit of roadPits) {
      const r = Math.hypot((x - pit.x) / pit.rx, (y - pit.y) / pit.ry);
      const weight = 1 - smooth(r);
      elevation -= pit.depth * weight;
      mud = Math.max(mud, weight);
    }
    const across = x - riverX(y);
    const shoulder = smooth((Math.abs(across) - 9) / 11) * (1 - smooth((Math.abs(across) - 26) / 11));
    const worksite = 1 - smooth(Math.abs(y - roadFord) / 17);
    elevation += shoulder * worksite * (0.43 * Math.sin(across * 0.53) + 0.19 * Math.sin(y * 0.39 + across * 0.29));
  }
  elevation = levelPad(elevation, x, y, start, 25, 56, 18);
  elevation = levelPad(elevation, x, y, goal, 21, 90, 48);
  const creek = includeWorksites ? creekColumns[x - creekStart] : undefined;
  let creekWater = false;
  if (creek) {
    const distance = Math.abs(y - creek.y);
    const halfWidth = creek.width * 0.5;
    const carving = smooth((x - creekStart) / 14) * (1 - smooth((distance - halfWidth) / 9));
    elevation = mix(elevation, Math.min(elevation, creek.z - creek.depth), carving);
    creekWater = distance < halfWidth + 5 && elevation < creek.z;
    mud = Math.max(mud, 0.60 * (1 - smooth((distance - halfWidth) / 9)));
  }
  for (const pile of piles) {
    if (!includeWorksites && pile === roadGravel) continue;
    const distance = Math.hypot(x - pile.x, y - pile.y);
    if (distance < pile.radius) {
      const deposit = pile.height * (1 - distance / pile.radius);
      return { elevation: elevation + deposit, deposit, pile, mud, water: false };
    }
  }
  return { elevation, mud, water: creekWater || (elevation < waterLevel(y) && distanceFromRiver < 32) };
}

function png(width, height, depth, colorType, scanlines) {
  const chunk = (type, data) => {
    const nameAndData = Buffer.concat([Buffer.from(type), data]);
    const result = Buffer.alloc(data.length + 12);
    result.writeUInt32BE(data.length, 0);
    nameAndData.copy(result, 4);
    result.writeUInt32BE(crc32(nameAndData), data.length + 8);
    return result;
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = depth;
  header[9] = colorType;
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(scanlines)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

const elevations = new Float32Array(size * size);
const mapPixels = Buffer.alloc(size * (size * 3 + 1));
const heightPixels = Buffer.alloc(size * (size * 2 + 1));
const materialColors = { soil: [169, 118, 74], gravel: [205, 201, 179], 'crushed-rock': [155, 168, 172] };
let low = Infinity;
let high = -Infinity;

for (let y = 0; y < size; y++) {
  for (let x = 0; x < size; x++) {
    const { elevation, mud, water, deposit, pile } = terrainAt(x, y);
    const index = y * size + x;
    const shade = x > 0 ? Math.max(-25, Math.min(25, (elevations[index - 1] - elevation) * 21)) : 0;
    elevations[index] = elevation;
    low = Math.min(low, elevation);
    high = Math.max(high, elevation);
    if (pile) pile.volumeM3 += deposit;

    const sample = Math.round(65535 * (elevation - minElevation) / (maxElevation - minElevation));
    heightPixels.writeUInt16BE(sample, y * (size * 2 + 1) + 1 + x * 2);
    let color = [76 + elevation * 0.75, 109 + elevation * 0.61, 67 + elevation * 0.69];
    if (mud > 0.15) color = color.map((channel, i) => mix(channel, [97, 63, 46][i], mud));
    if (water) color = [52, 109, 133];
    if (pile) color = materialColors[pile.material];
    const pixel = y * (size * 3 + 1) + 1 + x * 3;
    color.forEach((channel, i) => { mapPixels[pixel + i] = Math.round(channel + shade); });
  }
}

function paintDot(x, y, radius, color) {
  for (let dy = -radius; dy <= radius; dy++) {
    for (let dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy > radius * radius) continue;
      const pixel = (y + dy) * (size * 3 + 1) + 1 + (x + dx) * 3;
      mapPixels.set(color, pixel);
    }
  }
}

const trees = [];
for (let y = 5; y < size - 5; y += 10) {
  for (let x = 5; x < size - 5; x += 10) {
    const tx = x + Math.floor(random() * 7) - 3;
    const ty = y + Math.floor(random() * 7) - 3;
    const { mud, pile, water } = terrainAt(tx, ty, false);
    const density = 0.23 + 0.2 * Math.sin(tx / 83 + phase) * Math.cos(ty / 109);
    if (random() >= density || water || mud > 0.16 || pile) continue;
    if (Math.abs(tx - riverX(ty)) < 15) continue;
    if (Math.hypot(tx - start.x, ty - start.y) < 59 || Math.hypot(tx - goal.x, ty - goal.y) < 54) continue;
    // Preserve generation order because existing saves refer to trees by index.
    const creek = creekColumns[tx - creekStart];
    const nearBank = creek && Math.abs(ty - creek.y) < creek.width * 0.5 + 7;
    const bankSide = ty < (creek?.y ?? ty) ? -1 : 1;
    let treeY = nearBank ? Math.round(creek.y + bankSide * (creek.width * 0.5 + 9)) : ty;
    const awayFromDepot = Math.abs(tx - roadGravel.x) < roadGravel.radius + 2;
    if (awayFromDepot && Math.abs(treeY - roadGravel.y) < roadGravel.radius + 2) {
      treeY = roadGravel.y + (treeY >= roadGravel.y ? 1 : -1) * (roadGravel.radius + 2);
    }
    const elevation = terrainAt(tx, treeY).elevation;
    trees.push({ x: tx, y: treeY, z: +elevation.toFixed(2), scale: +(0.7 + random() * 0.7).toFixed(2) });
    paintDot(tx, treeY, 2, [32, 61, 44]);
  }
}
paintDot(start.x, start.y, 9, [250, 202, 79]);
paintDot(goal.x, goal.y, 9, [245, 103, 83]);

const world = {
  seed,
  dimensionsM: [size - 1, size - 1],
  heightmap: { file: 'height.png', samples: size, spacingM: 1, minElevation, maxElevation, observedRangeM: [low, high] },
  start: { ...start, z: 18 },
  goal: { ...goal, z: 48 },
  river: {
    centerline: Array.from({ length: 129 }, (_, i) => {
      const y = i * 8;
      return { x: +riverX(y).toFixed(2), y, z: +waterLevel(y).toFixed(2), width: +riverWidth(y).toFixed(2) };
    }),
    fords: fords.map((y, i) => ({ x: +riverX(y).toFixed(2), y, centerDepthM: fordDepths[i] })),
  },
  creek: {
    centerline: creekColumns.filter((_, i) => i % 4 === 0).map(({ x, y, z, width }) =>
      ({ x, y: +y.toFixed(3), z: +z.toFixed(3), width: +width.toFixed(3) })),
    crossing: { x: creekCrossingX, y: +creekAt(creekCrossingX).y.toFixed(3), centerDepthM: 0.33 },
  },
  roadContract: true,
  mudPits: [...mudPits, ...roadPits],
  piles: piles.map(pile => ({ ...pile, volumeM3: Math.round(pile.volumeM3) })),
  trees,
};

mkdirSync(output, { recursive: true });
writeFileSync(join(output, 'height.png'), png(size, size, 16, 0, heightPixels));
writeFileSync(join(output, 'overview.png'), png(size, size, 8, 2, mapPixels));
writeFileSync(join(output, 'world.json'), JSON.stringify(world, null, 2) + '\n');
console.log(`Seed ${seed}: saved ${size - 1} × ${size - 1} m of terrain, ${piles.length} piles and ${trees.length} trees to ${output}`);
