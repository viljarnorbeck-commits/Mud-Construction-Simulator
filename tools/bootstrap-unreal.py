"""Generate the cookable Unreal map and materials in Content/Generated."""

import sys
from pathlib import Path

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unreal_materials import ASSETS, FOLDER, build_materials
from unreal_foliage import build_foliage, build_spoil
from unreal_worker import build_worker
from unreal_audio import build_audio


def make_map():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    path = f"{FOLDER}/Valley"
    map_file = Path(unreal.Paths.project_content_dir()) / "Generated" / "Valley.umap"
    opened = levels.load_level(path) if map_file.exists() else levels.new_level(path)
    if not opened:
        raise RuntimeError("Could not open the Valley level")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = actors.get_all_level_actors()

    def ensure_actor(actor, location, rotation=unreal.Rotator()):
        result = next((item for item in existing if isinstance(item, actor)), None)
        if result is None:
            result = actors.spawn_actor_from_class(actor, unreal.Vector(*location), rotation)
        if result is None:
            raise RuntimeError(f"Could not spawn {actor}")
        return result

    angle = unreal.Rotator(pitch=-38, yaw=-63, roll=0)
    sun = ensure_actor(unreal.DirectionalLight, (0, 0, 200), angle)
    sun.set_actor_rotation(angle, False)
    sun_light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sun_light.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun_light.set_editor_property("intensity", 4.05)
    sun_light.set_light_color(unreal.LinearColor(1.0, 0.975, 0.925))
    sun_light.set_editor_property("atmosphere_sun_light", True)
    sun_light.set_editor_property("dynamic_shadow_distance_movable_light", 19000)
    sun_light.set_editor_property("light_source_angle", 1.35)
    sun_light.set_editor_property("dynamic_shadow_cascades", 4)
    sun_light.set_editor_property("contact_shadow_length", 0.008)
    sun_light.set_editor_property("shadow_bias", 0.65)

    sky = ensure_actor(unreal.SkyLight, (0, 0, 100))
    sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_light.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_light.set_editor_property("real_time_capture", True)
    sky_light.set_editor_property("intensity", 1.10)
    ensure_actor(unreal.SkyAtmosphere, (0, 0, 0))

    haze = ensure_actor(unreal.ExponentialHeightFog, (0, 0, 1300))
    fog = haze.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fog.set_editor_property("fog_density", 0.0095)
    fog.set_editor_property("fog_height_falloff", 0.28)
    fog.set_editor_property("start_distance", 2700)
    fog.set_volumetric_fog(False)
    ensure_actor(unreal.PlayerStart, (11500, 79500, 2070))
    if not levels.save_current_level():
        raise RuntimeError("Could not save the Valley map")


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.SyncToBrowser 0")
    ASSETS.make_directory(FOLDER)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    build_materials(asset_tools)
    build_foliage(asset_tools)
    build_spoil(asset_tools)
    build_worker(asset_tools)
    build_audio(asset_tools)
    make_map()
    unreal.log("Mud valley assets generated.")


if __name__ == "__main__":
    main()
