# Wheel loader

The procedural loader uses the Volvo L120H size class as its dimensional reference,
with a long-lift assembly adapted to the existing haulers. It is an original mesh
and linkage, not a reproduction of Volvo's proprietary Torque Parallel mechanism.

Reference: [Volvo L120H product guide, dimensions and hydraulics](https://www.volvoce.com/-/media/volvoce/global/products/wheel-loaders/wheel-loaders/brochures/product_guide_l120h_t4f_en_22_20062971_b.pdf).
The guide specifies a 3.3 m wheelbase, about 20 tonnes, 3 m bucket width,
5.4 s lift and 2.1 s tilt cycle. The long-boom option adds 450 mm of lift.

`LoaderRig.h` is the common centimetre-scale geometry for the mesh, cutting edge,
bucket pivot and closed two-link bell crank. Raised chassis towers carry paired
3 m arms. The bucket retains its attitude while lifting, within mechanical pitch
limits. The ram anchor and bell crank move with the arms. Both hydraulic circuits
share available flow, ramp in/out and slow under payload; the existing controls
are retained. The 2.9 m bucket has a hollow welded shell, cheeks and a cutting lip.

Dump clearance at full lift and 45 degrees is about 3.02 m above the chassis
datum, against approximately 2.59 m truck and 2.38 m dumper rims. Approach a hauler
sideways with its bed lowered, lift fully, roll forward into dump, then reverse.
Ground slope, rut sinkage and vehicle roll affect actual clearance.

Material now leaves buckets as the existing mass-bearing ballistic pour packets.
Swept collision handles the bed floor and walls in its current transform. Received
material joins the existing per-material cargo, axle load, rendering and save data.
An overflowing or raised bed sheds the remainder back into the terrain solver.
The saved falling-load schema is unchanged, and receivers register at spawn in the
ordinary world as well as diagnostics. The visible small spill particles remain
decorative; mass lives only in cargo, pour packets and soil columns.

Verification: `tools/run-physics-tests.sh` checks the entire linkage workspace and
ram envelopes; `tools/start-loader-validation.ps1` drives the production loader
through digging, rollback, reverse, haul, side approach and loading both haulers
in the normal world. It also checks retained cargo, airborne save/restore,
overflow and mass balance. This diagnostic never loads or overwrites user saves.
