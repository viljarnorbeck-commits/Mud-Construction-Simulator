"""Rebuild terrain materials, coherent cargo meshes and terrain lighting in UE 5.7."""
import sys
import runpy
from pathlib import Path
import unreal
sys.path.insert(0,str(Path(__file__).resolve().parent))
from unreal_materials import build_materials
from unreal_foliage import build_spoil
unreal.SystemLibrary.execute_console_command(None,"Interchange.FeatureFlags.Import.SyncToBrowser 0")
assets=unreal.AssetToolsHelpers.get_asset_tools()
build_materials(assets)
build_spoil(assets)
runpy.run_path(str(Path(__file__).with_name("bootstrap-unreal.py")))["make_map"]()
unreal.log("Terrain art rebuild completed.")
