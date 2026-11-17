"""Import the worker and its in-place animation clips for cooking."""

from pathlib import Path

import unreal

from unreal_materials import ASSETS, EDIT, FOLDER, Graph, PROPERTY
from worker_palette import FINISHES


def apply_worker_finish(asset_tools, destination, name, color, roughness, specular):
    graph = Graph(asset_tools, name, destination)
    EDIT.set_material_usage(graph.material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    base = graph.color(color)
    if name in ("Brown", "Brown2", "LightBrown", "Worker_Vest"):
        cloth = graph.node("Noise", scale=0.16, levels=1, output_min=0.88, output_max=1.08, turbulence=False)
        # Rest-pose position is read in the vertex shader and interpolated for the cloth shading.
        rest_position = graph.node("VertexInterpolator")
        graph.connect(graph.node("PreSkinnedPosition"), rest_position,
                      EDIT.get_material_expression_input_names(rest_position[0])[0])
        graph.connect(rest_position, cloth, EDIT.get_material_expression_input_names(cloth[0])[0])
        base = graph.multiply(base, cloth)
    graph.bind(base, PROPERTY.MP_BASE_COLOR)
    graph.bind(graph.scalar(roughness), PROPERTY.MP_ROUGHNESS)
    graph.bind(graph.scalar(specular), PROPERTY.MP_SPECULAR)
    graph.finish()


def import_fbx(asset_tools, source, destination, name, skeleton=None):
    importing_mesh = skeleton is None
    kind = (unreal.FBXImportType.FBXIT_SKELETAL_MESH if importing_mesh
            else unreal.FBXImportType.FBXIT_ANIMATION)
    options = unreal.FbxImportUI()
    for setting, value in {
        "automated_import_should_detect_type": False,
        "mesh_type_to_import": kind,
        "import_as_skeletal": True,
        "import_mesh": importing_mesh,
        "import_animations": not importing_mesh,
        "import_materials": importing_mesh,
        "import_textures": False,
        "create_physics_asset": False,
    }.items():
        options.set_editor_property(setting, value)
    if skeleton:
        options.set_editor_property("skeleton", skeleton)
    data = options.skeletal_mesh_import_data if importing_mesh else options.anim_sequence_import_data
    data.set_editor_property("force_front_x_axis", True)
    if importing_mesh:
        data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task = unreal.AssetImportTask()
    for setting, value in {
        "filename": str(source / f"{name}.fbx"),
        "destination_path": destination,
        "destination_name": name,
        "options": options,
        "automated": True,
        "replace_existing": True,
        "replace_existing_settings": True,
        "save": False,
    }.items():
        task.set_editor_property(setting, value)
    asset_tools.import_asset_tasks([task])
    asset = ASSETS.load_asset(f"{destination}/{name}")
    asset_class = unreal.SkeletalMesh if importing_mesh else unreal.AnimSequence
    if not isinstance(asset, asset_class):
        raise RuntimeError(f"Failed to import Worker {name}: {task.imported_object_paths}")
    if not ASSETS.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save Worker {name}")
    return asset


def build_worker(asset_tools, source=None):
    if source is None:
        source = Path(unreal.Paths.project_dir()) / "art" / "operator"
    source = source.resolve()
    destination = f"{FOLDER}/Worker"
    ASSETS.make_directory(destination)
    # The FBX factory imports clips against the exact skeleton already imported for the mesh.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        mesh = import_fbx(asset_tools, source, destination, "Worker")
        skeleton = mesh.get_editor_property("skeleton")
        for name, (color, roughness, specular) in FINISHES.items():
            apply_worker_finish(asset_tools, destination, name, color, roughness, specular)
        for name in ("Idle", "Walk", "Run", "Jump", "Land"):
            clip = import_fbx(asset_tools, source, destination, name, skeleton)
            clip.set_editor_property("enable_root_motion", False)
            clip.set_editor_property("force_root_lock", True)
            if not ASSETS.save_loaded_asset(clip, only_if_is_dirty=False):
                raise RuntimeError(f"Failed to save Worker animation {name}")
        if not ASSETS.save_directory(destination, only_if_is_dirty=False):
            raise RuntimeError("Failed to save the Worker skeleton and materials")
    finally:
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")
