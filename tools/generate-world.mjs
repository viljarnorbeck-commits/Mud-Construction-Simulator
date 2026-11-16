import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { deflateSync, crc32 } from 'node:zlib';

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

function hill(x, y, cx, cy, rx, ry, height) {
  return height * Math.exp(-(((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2));
}

function levelPad(elevation, x, y, pad, flatRadius, edgeRadius, level) {
  const distance = Math.hypot(x - pad.x, y - pad.y);
  return mix(level, elevation, smooth((distance - flatRadius) / (edgeRadius - flatRadius)));
}

function terrainAt(x, y) {
  const distanceFromRiver = Math.abs(x - riverX(y));
  const riverWeight = smooth((distanceFromRiver - riverWidth(y) / 2) / 24);
  const fordWeight = Math.max(...fords.map(ford => Math.exp(-((y - ford) ** 2) / 170)));
  const riverbed = waterLevel(y) - mix(2.9, 0.95, fordWeight);
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
  elevation = levelPad(elevation, x, y, start, 25, 56, 18);
  elevation = levelPad(elevation, x, y, goal, 21, 90, 48);
  for (const pile of piles) {
    const distance = Math.hypot(x - pile.x, y - pile.y);
    if (distance < pile.radius) {
      const deposit = pile.height * (1 - distance / pile.radius);
      return { elevation: elevation + deposit, deposit, pile, mud, water: false };
    }
  }
  return { elevation, mud, water: elevation < waterLevel(y) && distanceFromRiver < 32 };
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
    const { elevation, mud, pile, water } = terrainAt(tx, ty);
    const density = 0.23 + 0.2 * Math.sin(tx / 83 + phase) * Math.cos(ty / 109);
    if (random() >= density || water || mud > 0.16 || pile) continue;
    if (Math.abs(tx - riverX(ty)) < 15) continue;
    if (Math.hypot(tx - start.x, ty - start.y) < 59 || Math.hypot(tx - goal.x, ty - goal.y) < 54) continue;
    trees.push({ x: tx, y: ty, z: +elevation.toFixed(2), scale: +(0.7 + random() * 0.7).toFixed(2) });
    paintDot(tx, ty, 2, [32, 61, 44]);
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
    fords: fords.map(y => ({ x: +riverX(y).toFixed(2), y, centerDepthM: 0.95 })),
  },
  mudPits,
  piles: piles.map(pile => ({ ...pile, volumeM3: Math.round(pile.volumeM3) })),
  trees,
};

mkdirSync(output, { recursive: true });
writeFileSync(join(output, 'height.png'), png(size, size, 16, 0, heightPixels));
writeFileSync(join(output, 'overview.png'), png(size, size, 8, 2, mapPixels));
writeFileSync(join(output, 'world.json'), JSON.stringify(world, null, 2) + '\n');
console.log(`Seed ${seed}: saved ${size - 1} × ${size - 1} m of terrain, ${piles.length} piles and ${trees.length} trees to ${output}`);
