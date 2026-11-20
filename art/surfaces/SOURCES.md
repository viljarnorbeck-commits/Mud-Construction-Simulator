# Markmaterial

De fyra 2K-materialen används i marken och på lassens ytor. Färgtexturen innehåller albedo i RGB och råhet i alfakanalen. Normaltexturerna använder DirectX-orientering.

- Jord [Dirt Floor](https://polyhaven.com/a/dirt_floor)
- Blöt lera [Brown Mud 02](https://polyhaven.com/a/brown_mud_02)
- Grus [Gravel Floor 02](https://polyhaven.com/a/gravel_floor_02)
- Gräsmark [Grass Ground](https://polyhaven.com/a/grass_ground)

Materialen är hämtade från Poly Haven under [CC0](https://polyhaven.com/license). Krossberg använder grusskanningen i grövre skala med kallare färg.

`tools/prepare-surfaces.mjs` skapar reproducerbara, filtrerade 2048px-varianter i `Prepared/`.
Sand härleds från jordskanningen, lera från den våta lerskanningen och berg från gruset.
Färgkontrast och råhet filtreras separat; originalfilerna ändras inte. Dessa är bearbetade
CC0-material, inte ytterligare fotografiska skanningar. Se `TERRAIN-RENDERING.md` för rendering och verifiering.
