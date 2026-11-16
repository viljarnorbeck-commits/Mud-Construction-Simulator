"""Material compilation for the procedural valley."""

import math
import struct
import tempfile
import zlib
from pathlib import Path

import unreal

FOLDER = "/Game/Generated"
ASSETS = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
PROPERTY = unreal.MaterialProperty


def import_texture(asset_tools, file, normal=False):
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(file), "destination_path": FOLDER, "automated": True,
        "replace_existing": True, "save": False,
    }.items():
        task.set_editor_property(key, value)
    asset_tools.import_asset_tasks([task])
    imported = task.get_editor_property("imported_object_paths")
    if not imported:
        raise RuntimeError(f"Could not import {file}")
    texture = ASSETS.load_asset(imported[0])
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP
                                if normal else unreal.TextureCompressionSettings.TC_BC7)
    texture.set_editor_property("srgb", not normal)
    texture.set_editor_property("flip_green_channel", False)
    if not ASSETS.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {file}")
    return texture


class Graph:
    def __init__(self, asset_tools, name):
        self.name = name
        path = f"{FOLDER}/{name}"
        self.material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
        if self.material is None:
            self.material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
        if self.material is None:
            raise RuntimeError(f"Could not create {name}")
        EDIT.delete_all_material_expressions(self.material)

    def node(self, kind, **properties):
        expression = EDIT.create_material_expression(self.material, getattr(unreal, "MaterialExpression" + kind), 0, 0)
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression, ""

    def connect(self, source, target, pin):
        if not EDIT.connect_material_expressions(*source, target[0], pin):
            inputs = tuple(EDIT.get_material_expression_input_names(target[0]))
            raise RuntimeError(f"{self.name}: cannot connect {source[0].get_name()} {source[1]!r} to {pin}; inputs {inputs}")

    def bind(self, source, prop):
        if not EDIT.connect_material_property(*source, prop):
            raise RuntimeError(f"{self.name}: cannot connect material property {prop}")

    def scalar(self, value):
        return self.node("Constant", r=value)

    def color(self, rgb):
        return self.node("Constant3Vector", constant=unreal.LinearColor(*rgb, 1.0))

    def lerp(self, low, high, alpha):
        node = self.node("LinearInterpolate")
        self.connect(low, node, "A")
        self.connect(high, node, "B")
        self.connect(alpha, node, "Alpha")
        return node

    def multiply(self, left, right):
        node = self.node("Multiply")
        self.connect(left, node, "A")
        self.connect(right, node, "B")
        return node

    def uv(self, x_scale, y_scale=None):
        return self.node("TextureCoordinate", u_tiling=x_scale, v_tiling=x_scale if y_scale is None else y_scale)

    def sample(self, texture, uv, normal=False):
        kind = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
        node = self.node("TextureSample", texture=texture, sampler_type=kind)
        self.connect(uv, node, "UVs")
        return node

    def surface(self, maps, uv):
        encoded = self.sample(maps[0], uv)
        relief = self.sample(maps[1], uv, normal=True)
        return encoded, relief, (encoded[0], "A")

    def finish(self):
        EDIT.layout_material_expressions(self.material)
        errors = EDIT.recompile_material(self.material)
        if errors:
            raise RuntimeError(f"{self.name}: {errors}")
        if not ASSETS.save_loaded_asset(self.material, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {self.name}")


def blend(graph, left, right, factor):
    return tuple(graph.lerp(a, b, factor) for a, b in zip(left, right))


def make_ground(asset_tools, surfaces):
    g = Graph(asset_tools, "Ground")
    vertex = g.node("VertexColor")[0]
    grass, aggregate, variation, wetness = ((vertex, name) for name in ("R", "G", "B", "A"))
    # UVs are world metres. Keep the photographed texture scale close to the original surfaces.
    soil = g.surface(surfaces["Soil"], g.uv(0.69))
    mud = g.surface(surfaces["Mud"], g.uv(0.55))
    turf = g.surface(surfaces["Grass"], g.uv(0.47))
    gravel = g.surface(surfaces["Gravel"], g.uv(0.82))
    bare = blend(g, soil, mud, wetness)
    color, normal, roughness = blend(g, blend(g, bare, turf, grass), gravel, aggregate)
    patch_tone = g.lerp(g.scalar(0.69), g.scalar(1.28), variation)
    g.bind(g.multiply(color, patch_tone), PROPERTY.MP_BASE_COLOR)
    g.bind(normal, PROPERTY.MP_NORMAL)
    # The wet scan supplies the darker color. Water also lowers the surface roughness.
    dampness = g.lerp(g.scalar(1.04), g.scalar(0.67), wetness)
    g.bind(g.multiply(roughness, dampness), PROPERTY.MP_ROUGHNESS)
    g.finish()


def make_cargo(asset_tools, name, maps, tint, scale):
    g = Graph(asset_tools, name)
    color, relief, roughness = g.surface(maps, g.uv(scale))
    g.bind(g.multiply(color, g.color(tint)), PROPERTY.MP_BASE_COLOR)
    g.bind(relief, PROPERTY.MP_NORMAL)
    g.bind(roughness, PROPERTY.MP_ROUGHNESS)
    g.finish()


def water_texture():
    size = 256
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            u, v = x * math.tau / size, y * math.tau / size
            dx, dy = 0.0, 0.0
            for i, j, strength, phase in ((2, 5, 0.039, 0.0), (5, -3, 0.023, 2.0), (11, 7, 0.008, 1.4)):
                wave = math.cos(i * u + j * v + phase) * strength
                dx -= i * wave
                dy -= j * wave
            length = math.sqrt(1 + dx * dx + dy * dy)
            rows.extend(round(127.5 * (1 + axis / length)) for axis in (dx, dy, 1))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (bytes.fromhex("89504e470d0a1a0a")
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def make_water(asset_tools):
    with tempfile.TemporaryDirectory(prefix="MudWaves-") as folder:
        file = Path(folder) / "RiverWaves.png"
        file.write_bytes(water_texture())
        waves = import_texture(asset_tools, file, normal=True)
    g = Graph(asset_tools, "River")
    g.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    water = g.node("SingleLayerWaterMaterialOutput")
    g.connect(g.color((0.0007, 0.0011, 0.0010)), water, "ScatteringCoefficients")
    g.connect(g.color((0.0080, 0.0046, 0.0034)), water, "AbsorptionCoefficients")
    g.connect(g.scalar(0.08), water, "PhaseG")
    g.connect(g.color((0.90, 0.93, 0.91)), water, "ColorScaleBehindWater")
    broad = g.node("Panner", speed_x=0.012, speed_y=-0.052)
    fine = g.node("Panner", speed_x=-0.027, speed_y=-0.102)
    g.connect(g.uv(0.67, 0.35), broad, "Coordinate")
    g.connect(g.uv(1.30, 0.89), fine, "Coordinate")
    waves_a = g.sample(waves, broad, normal=True)
    waves_b = g.sample(waves, fine, normal=True)
    g.bind(g.lerp(waves_a, waves_b, g.scalar(0.33)), PROPERTY.MP_NORMAL)
    g.bind(g.color((0.025, 0.042, 0.043)), PROPERTY.MP_BASE_COLOR)
    g.bind(g.scalar(0.19), PROPERTY.MP_ROUGHNESS)
    g.bind(g.scalar(0.16), PROPERTY.MP_OPACITY)
    g.finish()


def make_palette(asset_tools):
    palette = {
        "Pine": ((0.035, 0.102, 0.066), 0.89, 0),
        "Bark": ((0.094, 0.048, 0.029), 0.91, 0),
        "Ochre": ((0.57, 0.30, 0.047), 0.56, 0.16),
        "Amber": ((0.71, 0.41, 0.063), 0.55, 0.14),
        "Teal": ((0.066, 0.28, 0.23), 0.53, 0.16),
        "Rubber": ((0.028, 0.030, 0.030), 0.90, 0),
        "Steel": ((0.15, 0.16, 0.16), 0.58, 0.38),
        "Glass": ((0.043, 0.080, 0.092), 0.22, 0.35),
        "GoldMarker": ((0.76, 0.48, 0.026), 0.55, 0),
        "RedMarker": ((0.80, 0.11, 0.04), 0.55, 0),
        "Headlight": ((0.86, 0.80, 0.64), 0.31, 0.2),
    }
    for name, (color, roughness, metallic) in palette.items():
        g = Graph(asset_tools, name)
        base = g.color(color)
        if name == "Pine":
            shade = g.lerp(g.scalar(0.72), g.scalar(1.37), g.node("PerInstanceRandom"))
            base = g.multiply(base, shade)
        g.bind(base, PROPERTY.MP_BASE_COLOR)
        g.bind(g.scalar(roughness), PROPERTY.MP_ROUGHNESS)
        g.bind(g.scalar(metallic), PROPERTY.MP_METALLIC)
        if name in ("GoldMarker", "RedMarker", "Headlight"):
            g.bind(g.multiply(base, g.scalar(0.38)), PROPERTY.MP_EMISSIVE_COLOR)
        g.finish()


def build_materials(asset_tools):
    folder = Path(__file__).resolve().parent.parent / "art" / "surfaces"
    surfaces = {
        name: (import_texture(asset_tools, folder / f"{name}_Color.png"),
               import_texture(asset_tools, folder / f"{name}_Normal.jpg", normal=True))
        for name in ("Soil", "Mud", "Grass", "Gravel")
    }
    make_ground(asset_tools, surfaces)
    make_cargo(asset_tools, "Loose", surfaces["Soil"], (1.08, 0.92, 0.82), 2.8)
    make_cargo(asset_tools, "LooseGravel", surfaces["Gravel"], (1.11, 1.05, 0.95), 2.4)
    make_cargo(asset_tools, "LooseRock", surfaces["Gravel"], (0.68, 0.77, 0.81), 1.3)
    make_water(asset_tools)
    make_palette(asset_tools)
