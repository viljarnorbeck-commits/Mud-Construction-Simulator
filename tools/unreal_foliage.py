"""Build reusable foliage and loose rock meshes for the valley."""

import math
import random
import tempfile
from pathlib import Path

import unreal

from unreal_materials import ASSETS, FOLDER


def pine_canopy():
    vertices, faces = [], []
    randomizer = random.Random(495)
    spokes = 12
    for tier, (height, radius, rise) in enumerate((
        (225, 204, 235), (312, 223, 243), (414, 187, 221),
        (521, 151, 211), (627, 108, 185), (717, 73, 173),
    )):
        start = len(vertices)
        lean_x, lean_y = tier * 5.8, tier * -2.3
        for spoke in range(spokes):
            angle = math.tau * (spoke / spokes + tier * 0.113)
            reach = radius * (0.70 + 0.20 * (spoke % 2) + randomizer.uniform(0, 0.19))
            vertices.append((lean_x + reach * math.cos(angle), lean_y + reach * math.sin(angle),
                             height + randomizer.uniform(-22, 18)))
        for spoke in range(spokes):
            angle = math.tau * (spoke / spokes + tier * 0.113)
            vertices.append((lean_x + radius * 0.53 * math.cos(angle), lean_y + radius * 0.53 * math.sin(angle),
                             height + rise * 0.47 + randomizer.uniform(-14, 16)))
        tip = len(vertices)
        vertices.extend(((lean_x + 16, lean_y - 8, height + rise), (lean_x, lean_y, height - 15)))
        for spoke in range(spokes):
            next_spoke = (spoke + 1) % spokes
            outer, next_outer = start + spoke, start + next_spoke
            inner, next_inner = outer + spokes, next_outer + spokes
            faces.extend(((outer, next_outer, next_inner), (outer, next_inner, inner),
                          (inner, next_inner, tip), (next_outer, outer, tip + 1)))
    return vertices, faces


def meadow_tuft():
    vertices, faces = [], []
    for blade in range(8):
        angle = blade * 2.4
        direction = (math.cos(angle), math.sin(angle))
        reach = 5 + blade % 4 * 5
        x, y = direction[0] * reach, direction[1] * reach
        across = (-direction[1], direction[0])
        height = 21 + (blade * 7 % 24)
        start = len(vertices)
        for fraction, width in ((0, 3.9), (0.60, 2.8)):
            lean = fraction * height * 0.33
            for side in (-1, 1):
                vertices.append((x + direction[0] * lean + across[0] * width * side,
                                 y + direction[1] * lean + across[1] * width * side, fraction * height))
        vertices.append((x + direction[0] * height * 0.57, y + direction[1] * height * 0.57, height))
        faces.extend(((start, start + 1, start + 2), (start + 1, start + 3, start + 2),
                      (start + 2, start + 3, start + 4)))
    return vertices, faces


def spoil_clump():
    vertices = [(0, 1, -18), (8, -11, 35)]
    faces = []
    outline = ((42, 0, -3), (26, 35, 5), (-16, 46, -6), (-47, 15, 1),
               (-39, -27, -8), (4, -46, 0), (39, -23, 8))
    vertices.extend(outline)
    for x, y, z in outline:
        vertices.append((x * 0.72 - 4, y * 0.66 + 2, z + 19))
    for index in range(len(outline)):
        a, b = 2 + index, 2 + (index + 1) % len(outline)
        c, d = a + len(outline), b + len(outline)
        faces.extend(((b, a, 0), (a, b, d), (a, d, c), (c, d, 1)))
    return vertices, faces


def import_mesh(asset_tools, directory, name, mesh):
    vertices, faces = mesh
    # Unreal's OBJ translator flips Y. Keep every mesh pivot at ground level.
    lines = [f"o {name}"]
    lines.extend(f"v {x:.4f} {-y:.4f} {z:.4f}" for x, y, z in vertices)
    max_height = max(z for _, _, z in vertices)
    lines.extend(f"vt {(x + 240) / 480:.4f} {z / max_height:.4f}" for x, _, z in vertices)
    # MeshDescription computes the face normal with the reversed cross product.
    lines.extend("f " + " ".join(f"{index + 1}/{index + 1}" for index in reversed(face)) for face in faces)
    file = directory / f"{name}.obj"
    file.write_text("\n".join(lines) + "\n", encoding="ascii")
    options = unreal.FbxImportUI()
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    mesh_options = options.get_editor_property("static_mesh_import_data")
    mesh_options.set_editor_property("combine_meshes", True)
    mesh_options.set_editor_property("auto_generate_collision", False)
    mesh_options.set_editor_property("generate_lightmap_u_vs", False)
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(file), "destination_path": FOLDER, "options": options,
        "automated": True, "replace_existing": True, "save": False,
    }.items():
        task.set_editor_property(key, value)
    asset_tools.import_asset_tasks([task])
    mesh = ASSETS.load_asset(f"{FOLDER}/{name}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Could not import mesh {name}")
    bounds = mesh.get_bounding_box()
    if bounds.max.z < max_height * 0.95 or bounds.max.z > max_height * 1.05 or bounds.min.z < -20:
        raise RuntimeError(f"Unexpected orientation or scale for {name}: {bounds}")
    if not ASSETS.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save mesh {name}")
    unreal.log(f"Terrain mesh verified: {name}, {len(faces)} triangles, {bounds.max.z:.0f} cm high.")


def build_foliage(asset_tools):
    # The commandlet has no content browser to synchronize with.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    with tempfile.TemporaryDirectory(prefix="MudFoliage-") as folder:
        directory = Path(folder)
        import_mesh(asset_tools, directory, "PineCanopy", pine_canopy())
        import_mesh(asset_tools, directory, "MeadowTuft", meadow_tuft())


def build_spoil(asset_tools):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    with tempfile.TemporaryDirectory(prefix="MudSpoil-") as folder:
        import_mesh(asset_tools, Path(folder), "SpoilClump", spoil_clump())
