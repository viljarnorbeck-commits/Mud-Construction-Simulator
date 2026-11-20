// Generation-time slope envelope. This lowers unsupported crests toward connected
// drainage; it never modifies live soil columns or claims to conserve gameplay mass.
// Eight-connected fast sweeps preserve channels and relief, unlike a global blur.
export function conditionSlopes(heights, size, limits) {
  const original=heights.slice(), diagonal=Math.SQRT2;
  // Sixteen directions reduce compass-aligned octagonal facets around incisions.
  const predecessors=[[-1,0,1],[0,-1,1],[-1,-1,diagonal],[1,-1,diagonal],
    [-2,-1,Math.sqrt(5)],[2,-1,Math.sqrt(5)],[-1,-2,Math.sqrt(5)],[1,-2,Math.sqrt(5)]];
  let passes=0, change=0;
  for(;passes<32;passes++) {
    change=0;
    for(const direction of [1,-1]) {
      const start=direction===1?0:size-1, end=direction===1?size:-1;
      for(let y=start;y!==end;y+=direction)for(let x=start;x!==end;x+=direction) {
        const i=y*size+x;let z=heights[i];
        for(const [dx,dy,d] of predecessors) {
          const nx=x+dx*direction,ny=y+dy*direction;if(nx<0||ny<0||nx>=size||ny>=size)continue;
          const j=ny*size+nx;z=Math.min(z,heights[j]+Math.min(limits[i],limits[j])*d);
        }
        change=Math.max(change,heights[i]-z);heights[i]=z;
      }
    }
    if(change<.0001)break;
  }
  let lowered=0,maxCorrection=0,maxRise=0,violations=0;
  for(let y=0;y<size;y++)for(let x=0;x<size;x++) {
    const i=y*size+x;lowered+=original[i]-heights[i];maxCorrection=Math.max(maxCorrection,original[i]-heights[i]);
    for(const [dx,dy,d] of [[1,0,1],[0,1,1],[1,1,diagonal],[-1,1,diagonal]]) {
      const nx=x+dx,ny=y+dy;if(nx<0||ny>=size||nx>=size)continue;
      const j=ny*size+nx,rise=Math.abs(heights[i]-heights[j])/d;
      maxRise=Math.max(maxRise,rise);if(rise>Math.min(limits[i],limits[j])+.0002)violations++;
    }
  }
  if(violations)throw Error(`Slope conditioning did not converge: ${violations} edges`);
  return {passes:passes+1,loweredTerrainM3:lowered,maxCorrectionM:maxCorrection,maxNeighbourGrade:maxRise};
}
