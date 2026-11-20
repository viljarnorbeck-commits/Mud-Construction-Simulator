"""Material compilation for the procedural valley."""

import math
import subprocess
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
    def __init__(self, asset_tools, name, folder=FOLDER):
        unreal.log(f"Material graph: {name}")
        self.name = name
        path = f"{folder}/{name}"
        self.material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
        if self.material is None:
            self.material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        if self.material is None:
            raise RuntimeError(f"Could not create {name}")
        # Unreal removes nodes while iterating the expression list and can skip some.
        remaining = EDIT.get_num_material_expressions(self.material)
        while remaining:
            EDIT.delete_all_material_expressions(self.material)
            count = EDIT.get_num_material_expressions(self.material)
            if count >= remaining:
                raise RuntimeError(f"{name}: could not clear the existing material graph")
            remaining = count

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
        unreal.log(f"Compiling material: {self.name}")
        errors = EDIT.recompile_material(self.material)
        unreal.log(f"Compiled material: {self.name}")
        if errors:
            raise RuntimeError(f"{self.name}: {errors}")
        if not ASSETS.save_loaded_asset(self.material, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {self.name}")

    def enable_instances(self):
        EDIT.set_material_usage(self.material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)


def blend(graph, left, right, factor):
    return tuple(graph.lerp(a, b, factor) for a, b in zip(left, right))


def attenuate_normal(g, normal, strength):
    depth = g.node("PixelDepth")
    subtract = g.node("Subtract")
    g.connect(g.scalar(8500), subtract, "A")
    g.connect(depth, subtract, "B")
    fade = g.node("Saturate")
    g.connect(g.multiply(subtract, g.scalar(1 / 7000)), fade, "")
    return g.lerp(g.color((0, 0, 1)), normal, g.multiply(fade, g.scalar(strength)))


def custom_node(g, code, output, values):
    inputs=[]
    for name in values:
        entry=unreal.CustomInput(); entry.set_editor_property("input_name",name); inputs.append(entry)
    result=g.node("Custom",code=code,output_type=output,inputs=inputs)
    for name,value in values.items(): g.connect(value,result,name)
    return result


def ground_surface(g, maps, scale, strength, weight):
    world=g.node("WorldPosition"); normal_ws=g.node("VertexNormalWS")
    encoded=custom_node(g,"""
[branch] if(Weight<0.0001) return 0;
float3 p=World*Scale*0.01;
float3 w=pow(abs(N),8); w/=max(w.x+w.y+w.z,0.0001);
float4 c=Texture2DSampleGrad(Tex,TexSampler,p.xy,ddx(p.xy),ddy(p.xy));
[branch] if(w.z<0.995) c=c*w.z+
 Texture2DSampleGrad(Tex,TexSampler,p.yz,ddx(p.yz),ddy(p.yz))*w.x+
 Texture2DSampleGrad(Tex,TexSampler,p.xz,ddx(p.xz),ddy(p.xz))*w.y;
return c;
""",unreal.CustomMaterialOutputType.CMOT_FLOAT4,
        {"Tex":g.node("TextureObject",texture=maps[0]),"World":world,"N":normal_ws,
         "Scale":g.scalar(scale),"Weight":weight})
    relief=custom_node(g,"""
float3 flat=float3(0,0,1);
float fade=saturate((8500-Depth)/7000)*Strength*N.z*N.z;
[branch] if(Weight<0.0001 || fade<0.005) return flat;
float2 uv=World.xy*Scale*0.01;
float2 xy=Texture2DSampleGrad(Tex,TexSampler,uv,ddx(uv),ddy(uv)).rg*2-1;
return lerp(flat,float3(xy,sqrt(saturate(1-dot(xy,xy)))),fade);
""",unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        {"Tex":g.node("TextureObject",texture=maps[1],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
         "World":world,"N":normal_ws,"Scale":g.scalar(scale),"Weight":weight,
         "Depth":g.node("PixelDepth"),"Strength":g.scalar(strength)})
    color=g.node("ComponentMask",r=True,g=True,b=True,a=False)
    roughness=g.node("ComponentMask",r=False,g=False,b=False,a=True)
    g.connect(encoded,color,""); g.connect(encoded,roughness,"")
    return color,relief,roughness


def make_ground(asset_tools, surfaces):
    g = Graph(asset_tools, "Ground")
    vertex = g.node("VertexColor")[0]
    grass, gravel, variation, wet = ((vertex,name) for name in ("R","G","B","A"))
    minerals = g.node("TextureCoordinate", coordinate_index=2)
    rock_coord = g.node("TextureCoordinate", coordinate_index=3)
    def component(source, channel):
        node = g.node("ComponentMask", r=channel==0, g=channel==1, b=False, a=False)
        g.connect(source,node,""); return node
    sand, clay, rock = component(minerals,0), component(minerals,1), component(rock_coord,0)
    weights = [grass,gravel,sand,clay,rock]
    total = weights[0]
    for weight in weights[1:]:
        add=g.node("Add"); g.connect(total,add,"A"); g.connect(weight,add,"B"); total=add
    remaining=g.node("OneMinus"); g.connect(total,remaining,"")
    dry=g.node("OneMinus"); g.connect(wet,dry,"")
    soil=ground_surface(g,surfaces["Soil"],0.60,0.30,g.multiply(remaining,dry))
    mud=ground_surface(g,surfaces["Mud"],0.52,0.16,g.multiply(remaining,wet))
    entries=[(blend(g,soil,mud,wet),remaining)]
    for name,weight,scale,strength in (("Grass",grass,.45,.20),("Gravel",gravel,.74,.46),
        ("Sand",sand,.48,.055),("Clay",clay,.43,.10),("Rock",rock,.27,.48)):
        entries.append((ground_surface(g,surfaces[name],scale,strength,weight),weight))
    result=[]
    for channel in range(3):
        summed=None
        for surface,weight in entries:
            value=g.multiply(surface[channel],weight)
            if summed is None: summed=value
            else:
                add=g.node("Add"); g.connect(summed,add,"A"); g.connect(value,add,"B"); summed=add
        result.append(summed)
    color,normal,roughness=result
    work=g.node("TextureCoordinate",coordinate_index=1)
    cut,fill=component(work,0),component(work,1)
    color=g.lerp(color,g.multiply(color,g.color((.92,.87,.81))),cut)
    color=g.lerp(color,g.multiply(color,g.color((1.04,1.025,.99))),fill)
    color=g.multiply(color,g.lerp(g.scalar(.94),g.scalar(1.06),variation))
    g.bind(color,PROPERTY.MP_BASE_COLOR)
    normalized=g.node("Normalize"); g.connect(normal,normalized,"")
    g.bind(normalized,PROPERTY.MP_NORMAL)
    g.bind(g.lerp(roughness,g.scalar(.48),wet),PROPERTY.MP_ROUGHNESS)
    g.bind(g.scalar(.25),PROPERTY.MP_SPECULAR)
    g.finish()


def make_cargo(asset_tools, name, maps, tint, scale, instanced=True):
    g = Graph(asset_tools, name)
    if instanced:
        g.enable_instances()
    color, relief, roughness = g.surface(maps, g.uv(scale))
    g.bind(g.multiply(color, g.color(tint)), PROPERTY.MP_BASE_COLOR)
    g.bind(attenuate_normal(g, relief, 0.30), PROPERTY.MP_NORMAL)
    g.bind(roughness, PROPERTY.MP_ROUGHNESS)
    g.finish()


def make_cargo_materials(asset_tools, surfaces):
    for material, deposit, maps, tint, scale in (
        ("Loose", "PayloadSoil", surfaces["Soil"], (0.92, 0.85, 0.76), 1.1),
        ("LooseGravel", "PayloadGravel", surfaces["Gravel"], (1.06, 1.04, 1.00), 1.4),
        ("LooseRock", "PayloadRock", surfaces["Rock"], (0.88, 0.93, 1.00), 0.8),
        ("LooseSand", "PayloadSand", surfaces["Sand"], (1.04, 1.01, 0.94), 0.8),
        ("LooseClay", "PayloadClay", surfaces["Clay"], (0.96, 0.89, 0.83), 0.9),
    ):
        make_cargo(asset_tools, material, maps, tint, scale)
        make_cargo(asset_tools, deposit, maps, tint, scale, instanced=False)


def water_texture():
    size = 256
    # Several directions and wavelengths break up the repeating highlights at the ford.
    ripples = (
        (1, 3, 0.031, 0.3),
        (-2, 5, 0.018, 1.1),
        (4, 3, 0.010, 2.7),
        (3, -7, 0.009, 2.0),
        (-6, 8, 0.005, 0.6),
        (11, 7, 0.003, 1.4),
    )
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            u, v = x * math.tau / size, y * math.tau / size
            dx, dy = 0.0, 0.0
            for i, j, strength, phase in ripples:
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
    g.connect(g.color((0.00085, 0.00145, 0.00150)), water, "ScatteringCoefficients")
    g.connect(g.color((0.0070, 0.0040, 0.0030)), water, "AbsorptionCoefficients")
    g.connect(g.scalar(0.08), water, "PhaseG")
    g.connect(g.color((0.96, 0.97, 0.94)), water, "ColorScaleBehindWater")
    broad = g.node("Panner", speed_x=0.012, speed_y=-0.052)
    fine = g.node("Panner", speed_x=-0.027, speed_y=-0.102)
    g.connect(g.uv(0.67, 0.35), broad, "Coordinate")
    g.connect(g.uv(1.30, 0.89), fine, "Coordinate")
    waves_a = g.sample(waves, broad, normal=True)
    waves_b = g.sample(waves, fine, normal=True)
    ripples = g.lerp(waves_a, waves_b, g.scalar(0.33))
    g.bind(g.lerp(g.color((0, 0, 1)), ripples, g.scalar(0.82)), PROPERTY.MP_NORMAL)
    g.bind(g.color((0.031, 0.052, 0.055)), PROPERTY.MP_BASE_COLOR)
    g.bind(g.scalar(0.30), PROPERTY.MP_ROUGHNESS)
    g.bind(g.scalar(0.13), PROPERTY.MP_OPACITY)
    g.finish()


def make_palette(asset_tools, mud_maps):
    palette = {
        "Pine": ((0.032, 0.095, 0.042), 0.91, 0),
        "Bark": ((0.088, 0.051, 0.032), 0.91, 0),
        "Meadow": ((0.11, 0.17, 0.052), 0.91, 0),
        "Ochre": ((0.42, 0.20, 0.020), 0.48, 0.0),
        "Amber": ((0.52, 0.28, 0.024), 0.43, 0.0),
        "Teal": ((0.195, 0.275, 0.055), 0.41, 0.0),
        "Rubber": ((0.018, 0.020, 0.021), 0.88, 0.0),
        "Tyre": ((0.021, 0.023, 0.024), 0.81, 0.0),
        "Chassis": ((0.052, 0.056, 0.057), 0.60, 0.16),
        "Steel": ((0.22, 0.23, 0.24), 0.51, 0.77),
        "WornSteel": ((0.135, 0.13, 0.122), 0.62, 0.71),
        "CuttingEdge": ((0.32, 0.33, 0.34), 0.34, 0.90),
        "CylinderRod": ((0.52, 0.56, 0.60), 0.20, 0.96),
        "Glass": ((0.014, 0.025, 0.031), 0.19, 0.0),
        "GoldMarker": ((0.76, 0.48, 0.026), 0.55, 0),
        "RedMarker": ((0.80, 0.11, 0.04), 0.55, 0),
        "Headlight": ((0.86, 0.80, 0.64), 0.31, 0.2),
        "AmberLamp": ((0.79, 0.26, 0.019), 0.26, 0.0),
        "RedLamp": ((0.43, 0.021, 0.012), 0.27, 0.0),
    }
    dirt = {
        "Ochre": (0.12, 0.30), "Amber": (0.10, 0.23), "Teal": (0.13, 0.29),
        "Chassis": (0.09, 0.31), "Tyre": (0.08, 0.37), "WornSteel": (0.19, 0.51),
    }
    for name, (color, roughness, metallic) in palette.items():
        g = Graph(asset_tools, name)
        if name in ("Pine", "Bark", "Meadow", "Ochre", "Teal", "Rubber", "Tyre", "Chassis",
                    "Steel", "WornSteel", "CuttingEdge", "AmberLamp"):
            g.enable_instances()
        base = g.color(color)
        rough = g.scalar(roughness)
        metal = g.scalar(metallic)
        if name == "Pine":
            base = g.lerp(g.multiply(base, g.scalar(0.68)), g.color((0.074, 0.134, 0.056)),
                          g.node("PerInstanceRandom"))
        if name == "Meadow":
            g.material.set_editor_property("two_sided", True)
            g.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            base = g.lerp(base, g.color((0.25, 0.25, 0.087)), g.node("PerInstanceRandom"))
            fade = g.node("MaterialFunctionCall")
            fade_function = ASSETS.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA")
            fade[0].set_material_function(fade_function)
            alpha = next(pin for pin in EDIT.get_material_expression_input_names(fade[0])
                         if str(pin).replace(" ", "").lower().startswith("alphathreshold"))
            g.connect(g.node("PerInstanceFadeAmount"), fade, alpha)
            g.bind(fade, PROPERTY.MP_OPACITY_MASK)
        if name in dirt:
            low, high = dirt[name]
            mud, normal, mask = g.surface(mud_maps, g.uv(1.65))
            amount = g.lerp(g.scalar(low), g.scalar(high), mask)
            base = g.lerp(base, g.multiply(mud, g.color((0.68, 0.56, 0.43))), amount)
            rough = g.lerp(rough, g.scalar(0.83), amount)
            metal = g.lerp(metal, g.scalar(0), amount)
            if name in ("Tyre", "WornSteel"):
                g.bind(g.lerp(g.color((0, 0, 1)), normal, g.scalar(0.30)), PROPERTY.MP_NORMAL)
        if name == "Glass":
            edge = g.node("Fresnel")
            base = g.lerp(base, g.color((0.095, 0.16, 0.19)), edge)
            g.bind(g.scalar(0.62), PROPERTY.MP_SPECULAR)
        g.bind(base, PROPERTY.MP_BASE_COLOR)
        g.bind(rough, PROPERTY.MP_ROUGHNESS)
        g.bind(metal, PROPERTY.MP_METALLIC)
        if name in ("GoldMarker", "RedMarker", "Headlight", "AmberLamp", "RedLamp"):
            glow = 0.20 if name in ("AmberLamp", "RedLamp") else 0.38
            g.bind(g.multiply(base, g.scalar(glow)), PROPERTY.MP_EMISSIVE_COLOR)
        g.finish()


def make_cab_glazing(asset_tools):
    g = Graph(asset_tools, "CabGlazing")
    g.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    g.material.set_editor_property("two_sided", True)
    g.material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE)
    edge = g.node("Fresnel")
    tint = g.lerp(g.color((0.053, 0.090, 0.098)), g.color((0.27, 0.38, 0.41)), edge)
    g.bind(tint, PROPERTY.MP_BASE_COLOR)
    g.bind(g.lerp(g.scalar(0.19), g.scalar(0.62), edge), PROPERTY.MP_OPACITY)
    g.bind(g.scalar(0.19), PROPERTY.MP_ROUGHNESS)
    g.bind(g.scalar(0.72), PROPERTY.MP_SPECULAR)
    g.bind(g.multiply(tint, g.lerp(g.scalar(0.02), g.scalar(0.11), edge)), PROPERTY.MP_EMISSIVE_COLOR)
    g.finish()


def make_cab_interiors(asset_tools):
    finishes = {
        "CabTrim": ((0.073, 0.079, 0.083), 0.71),
        "CabFloor": ((0.027, 0.030, 0.033), 0.88),
        "CabLiner": ((0.24, 0.23, 0.20), 0.95),
        "CabCloth": ((0.071, 0.082, 0.093), 0.94),
        "CabLCD": ((0.018, 0.035, 0.053), 0.31),
    }
    for name, (color, roughness) in finishes.items():
        g = Graph(asset_tools, name)
        base = g.color(color)
        if name == "CabFloor":
            g.enable_instances()
        if name == "CabCloth":
            # A small two-directional weave keeps the cushions matte at cabin distances.
            uv = g.uv(170)
            threads = []
            for horizontal in (False, True):
                channel = g.node("ComponentMask", r=horizontal, g=not horizontal, b=False, a=False)
                g.connect(uv, channel, "")
                wave = g.node("Sine")
                g.connect(channel, wave, "")
                threads.append(wave)
            weave = g.multiply(threads[0], threads[1])
            base = g.lerp(base, g.multiply(base, g.scalar(1.08)), weave)
        if name == "CabLCD":
            g.bind(g.multiply(base, g.scalar(0.42)), PROPERTY.MP_EMISSIVE_COLOR)
        g.bind(base, PROPERTY.MP_BASE_COLOR)
        g.bind(g.scalar(roughness), PROPERTY.MP_ROUGHNESS)
        g.finish()


def build_materials(asset_tools):
    folder = Path(__file__).resolve().parent.parent / "art" / "surfaces"
    subprocess.run(["node", str(Path(__file__).with_name("prepare-surfaces.mjs"))], check=True)
    normal_source = {"Sand": "Soil", "Clay": "Mud", "Rock": "Gravel"}
    surfaces = {
        name: (import_texture(asset_tools, folder / "Prepared" / f"{name}_Color.png"),
               import_texture(asset_tools, folder / f"{normal_source.get(name, name)}_Normal.jpg", normal=True))
        for name in ("Soil", "Mud", "Grass", "Gravel", "Sand", "Clay", "Rock")
    }
    make_ground(asset_tools, surfaces)
    make_cargo_materials(asset_tools, surfaces)
    make_water(asset_tools)
    make_palette(asset_tools, surfaces["Mud"])
    make_cab_glazing(asset_tools)
    make_cab_interiors(asset_tools)
