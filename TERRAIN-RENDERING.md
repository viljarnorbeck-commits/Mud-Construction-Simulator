# Coherent terrain surfaces

Reference study: [Skyturns' Dirt Town Demo screenshots and gameplay trailer](https://store.steampowered.com/app/5471710/Dirt_Town_Demo/).
The useful references were readable banks, material separation, aggregate structure and flowing earth.
No reference-game assets or code are included.

## Geometry and contact

The previous surface used metre-scale triangles, coarse skirts and four shared material channels.
`TerrainSurface.h` now reconstructs a monotone surface from the existing physical columns.
It preserves column heights and bounds the interpolation, with no added geometric noise.
The near mesh, vehicle height queries and near collision mesh share half-metre triangles.
Simulation layers, packing, material inventories and the saved column schema are unchanged.

Rendering uses 0.5/1/2/4 m spacing with distance hysteresis. Every LOD has the same
half-metre boundary samples and global normals. Boundary fans replace skirts and T-junctions.
Near collision uses the detailed surface; distant LOD interiors approximate it as usual.
Reconstruction samples are cached and invalidated around edits and on restoration.
The existing two-chunk update limit is retained. Near meshes also keep their topology and
reuse untouched vertex attributes, reconstructing only an edited rectangle and its normal
halo. Sharp cuts remain in the source data.

## Materials and moving loads

Sand, clay and rock have separate interpolated masks, alongside soil, grass, gravel and wetness.
The CC0 scans keep their 2048px resolution. `prepare-surfaces.mjs` filters high-frequency
albedo contrast, bounds roughness and derives smoother sand/clay surfaces from the original
scans. Originals remain intact. Prepared outputs are reproducible and ignored by Git.

Ground shaders use weighted triplanar projection on steep faces, explicit texture gradients,
distance-faded normal relief and branches that skip absent material layers. Broad variation
and work-finish tinting are restrained. Wet material has a moderate roughness response.
Cargo materials use the same prepared sources with stable mesh UVs.

Soft-earth crumbs are smaller and rounded. Sand and clay have continuous cargo surfaces
without angular rock scatter. Aggregate retains restrained angular fragments. The dozer bank
has a denser mesh and broader moving folds. Falling surfaces are rebuilt from the existing
mass-bearing pour packets; decorative particles never create material inventories.
Reusable mesh buffers avoid recreation when packet topology is unchanged.

Lighting uses four cascades, conservative shadow bias and short contact shadows. Texture
filtering and sharpening remain bounded to avoid amplifying grain.

## Reproduction and verification

Build with `tools/build-windows.ps1 -Target Editor`; header generation is forced to prevent
stale reflected class layouts after structural changes. For an existing editor build, run
`tools/rebuild-terrain-art.py` using Unreal's Python commandlet, for example:

```powershell
& "$engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PWD/tools/rebuild-terrain-art.py" -NoLiveCoding -unattended -NullRHI
```

`tools/render-material-review.ps1` produces near, overview and work PNGs with fixed cameras,
exposure and fixtures. It never loads or overwrites a user profile. Optional `-Width 3840
-Height 2160 -Offscreen` verifies native 4K output. PNG dimensions were checked.

Measured on the local RTX 4070 Laptop GPU, at 100% render scale in the fixed near scene:

| Version | Output | Mean frame | P95 frame | Mean FPS |
| --- | --- | --- | --- | --- |
| Before | 1920×1080 | 5.734 ms | 6.755 ms | 174.4 |
| Final | 1920×1080 | 5.879 ms | 7.268 ms | 170.1 |
| Final | 3840×2160 | 19.987 ms | 21.155 ms | 50.0 |

These are short scene measurements, not a guarantee for every workload. Native 4K/120 FPS
is not achieved. The existing performance/upscaling mode remains available.

The full-chunk update path measured 24.910 ms mean / 26.442 ms P95 (40.1 FPS) during the
active dozer/pour fixture at 1080p. This prompted the final local-rectangle optimization.
That optimization compiles, but its final runtime and performance verification are pending:
Windows App Control rejected the new unsigned DLL with error 4551 on both the host and the
isolated Windows Sandbox. No security protections were disabled. There is no measured FPS
claim for the final local-update path, and the PR remains a draft.

All eight standalone physics suites passed, including monotonic reconstruction, plane
preservation, bounded cuts and exact column datums. Native verification passed for:

- Highlands machine regression and environmental/vehicle acceptance.
- Loader scoop/transport/tip into both production haulers, cargo retention and overflow.
- Soil transport, compaction, ruts, restoration and mass preservation.
- 10,444 rendered fine-mesh/boundary samples against contact height, with maximum error
  printed as 0.00000000 m; fractional sample restoration also passed.
- A temporary legacy-world fixture with the road contract explicitly enabled: the truck
  traversed 47.0 m of built gravel road, while raw terrain, soil fill and an ungraded ridge failed.

These native results and screenshots cover the material/reconstruction implementation before
the final local-rectangle optimization. A native check of that incremental path is included
in `MudSurfaceValidation.cpp`; running it on an allowed build is the remaining verification.

Local comparison artifacts are under `Saved/MaterialComparison/Before`, `Optimized` and `4K`.
The surface remains a heightfield: overhangs and individual physical sand grains are outside
this change. The one-metre physical column grid still limits excavation detail.
