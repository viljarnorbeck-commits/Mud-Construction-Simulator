"""Generate and import the demo's original sound library as cookable Sound Waves."""

from pathlib import Path

import unreal

from synthesize_audio import CUES, LOOPS, build
from unreal_materials import ASSETS, FOLDER


def build_audio(asset_tools):
    source = Path(unreal.Paths.project_saved_dir()) / "GeneratedAudio"
    destination = f"{FOLDER}/Audio"
    ASSETS.make_directory(destination)
    build(source)
    tasks = []
    for name in (*LOOPS, *CUES):
        task = unreal.AssetImportTask()
        task.filename = str((source / f"{name}.wav").resolve())
        task.destination_path = destination
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        tasks.append(task)
    asset_tools.import_asset_tasks(tasks)
    for name in (*LOOPS, *CUES):
        sound = ASSETS.load_asset(f"{destination}/{name}")
        if not isinstance(sound, unreal.SoundWave):
            raise RuntimeError(f"Failed to import audio {name}")
        sound.set_editor_property("looping", name in LOOPS)
        if not ASSETS.save_loaded_asset(sound, only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save audio {name}")
    unreal.log(f"Valley audio generated: {len(LOOPS)} loops and {len(CUES)} one-shot sounds.")
