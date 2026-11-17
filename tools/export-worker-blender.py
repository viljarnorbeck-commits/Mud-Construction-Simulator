"""Export the CC0 Worker and its in-place clips. Run with Blender and pass the original GLB after --."""

import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worker_palette import FINISHES


def smoothstep(low, high, value):
    t = min(1.0, max(0.0, (value - low) / (high - low)))
    return t * t * (3 - 2 * t)


def set_finishes():
    for name, (color, roughness, specular) in FINISHES.items():
        material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        material.diffuse_color = (*color, 1.0)
        material.use_nodes = True
        surface = next(node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
        surface.inputs["Base Color"].default_value = (*color, 1.0)
        surface.inputs["Roughness"].default_value = roughness
        surface.inputs["Specular IOR Level"].default_value = specular


def refine_worker_part(obj):
    mesh = obj.data
    world = obj.matrix_world
    local = world.inverted()
    is_body = obj.name == "Worker_Body"
    is_head = obj.name == "Worker_Head"
    if is_body:
        for index, material in enumerate(mesh.materials):
            if material.name == "Worker_Yellow":
                mesh.materials[index] = bpy.data.materials["Worker_Tape"]
                break
        mesh.materials.append(bpy.data.materials["Worker_Gloves"])
        glove_index = len(mesh.materials) - 1

    surface = bmesh.new()
    try:
        surface.from_mesh(mesh)
        bmesh.ops.remove_doubles(surface, verts=list(surface.verts), dist=0.00002)
        if is_body:
            cuff = 0.566
            for side in (-1, 1):
                bmesh.ops.bisect_plane(
                    surface, geom=[*surface.verts, *surface.edges, *surface.faces],
                    dist=0.00001, plane_co=local @ Vector((side * cuff, 0, 0)),
                    plane_no=world.to_3x3().transposed() @ Vector((1, 0, 0)),
                )
            for face in surface.faces:
                point = world @ face.calc_center_median()
                if mesh.materials[face.material_index].name == "Skin" and abs(point.x) > cuff:
                    face.material_index = glove_index
        bmesh.ops.join_triangles(
            surface, faces=list(surface.faces), cmp_materials=True,
            angle_face_threshold=math.radians(43), angle_shape_threshold=math.radians(54),
        )
        for vertex in surface.verts:
            point = world @ vertex.co
            if is_body:
                # Taper the palms and knuckles into the original wrist without moving the rig.
                weight = smoothstep(0.548, 0.615, abs(point.x))
                point.x -= math.copysign(max(0.0, abs(point.x) - 0.548) * 0.16 * weight, point.x)
                point.y += (-0.087 - point.y) * 0.14 * weight
                point.z += (1.432 - point.z) * 0.12 * weight
            elif is_head:
                weight = smoothstep(1.47, 1.62, point.z)
                point.x *= 1.0 - 0.050 * weight
                point.y += (-0.050 - point.y) * 0.055 * weight
                point.z -= max(0.0, point.z - 1.51) * 0.065 * weight
            vertex.co = local @ point
        surface.to_mesh(mesh)
    finally:
        surface.free()

    for polygon in mesh.polygons:
        polygon.use_smooth = True
    if mesh.has_custom_normals:
        mesh.normals_split_custom_set([(0, 0, 0)] * len(mesh.loops))
    bpy.context.view_layer.objects.active = obj
    rounded = obj.modifiers.new("Rounded workwear", "SUBSURF")
    rounded.subdivision_type = "CATMULL_CLARK"
    rounded.levels = 1
    # Apply before the armature so its vertex weights interpolate with the surface.
    bpy.ops.object.modifier_move_up(modifier=rounded.name)
    bpy.ops.object.modifier_apply(modifier=rounded.name)


def activate(armature, action):
    armature.animation_data.action = action
    armature.animation_data.action_slot = action.slots[0]


def export_file(path, scene, rig, action=None):
    if action:
        activate(rig, action)
        scene.frame_start, scene.frame_end = (round(frame) for frame in action.frame_range)
    else:
        rig.animation_data.action = None
    bpy.ops.export_scene.fbx(
        filepath=str(path),
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        add_leaf_bones=False,
        mesh_smooth_type="FACE",
        axis_forward="-Y",
        axis_up="Z",
        bake_anim=action is not None,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_simplify_factor=0.0,
    )


def rotate_in_place(bone, rotation):
    transform = bone.matrix.copy()
    rotated = rotation.to_matrix().to_4x4() @ transform
    rotated.translation = transform.translation
    bone.matrix = rotated


def position_leg(rig, side, ankle, foot_pitch):
    bones = rig.pose.bones
    upper, lower, foot = (bones[name + "." + side] for name in ("UpperLeg", "LowerLeg", "Foot"))
    thigh = (lower.bone.head_local - upper.bone.head_local).length
    shin = (foot.bone.head_local - lower.bone.head_local).length
    hip = upper.head.copy()
    span = ankle - hip
    distance = min(span.length, thigh + shin - 0.000005)
    along = span.normalized()
    forward = Vector((0, -1, 0))
    bend = (forward - along * forward.dot(along)).normalized()
    run = (thigh * thigh - shin * shin + distance * distance) / (2 * distance)
    knee = hip + along * run + bend * math.sqrt(max(0, thigh * thigh - run * run))

    rotate_in_place(upper, (lower.head - hip).rotation_difference(knee - hip))
    bpy.context.view_layer.update()
    shin_axis = lower.bone.matrix_local.to_3x3().inverted() @ (foot.bone.head_local - lower.bone.head_local)
    shin_pose = lower.matrix.to_3x3() @ shin_axis
    target = hip + along * distance
    rotate_in_place(lower, shin_pose.rotation_difference(target - knee))
    rotate_in_place(foot, Quaternion((1, 0, 0), math.radians(foot_pitch)))
    pose = foot.matrix.copy()
    pose.translation = target
    foot.matrix = pose


def author_clip(name, keys, rig, idle):
    scene = bpy.context.scene
    to_rig = rig.matrix_world.inverted().to_3x3()
    activate(rig, idle)
    scene.frame_set(0)
    neutral = {bone.name: bone.matrix_basis.copy() for bone in rig.pose.bones}
    ankle = {side: rig.pose.bones["Foot." + side].head.copy() for side in ("L", "R")}
    action = bpy.data.actions.new(name)
    rig.animation_data.action = action
    for frame, body_z, left, right, left_arm, right_arm, lean in keys:
        scene.frame_set(frame)
        for bone in rig.pose.bones:
            bone.matrix_basis = neutral[bone.name]
        bpy.context.view_layer.update()
        body = rig.pose.bones["Body"]
        pose = body.matrix.copy()
        pose.translation += to_rig @ Vector((0, 0, body_z * 0.01))
        body.matrix = pose
        rotate_in_place(rig.pose.bones["Torso"], Quaternion((1, 0, 0), math.radians(lean)))
        bpy.context.view_layer.update()
        for side, leg, swing in (("L", left, left_arm), ("R", right, right_arm)):
            ahead, lift, pitch = leg
            ankle_offset = to_rig @ (Vector((0, -ahead, lift)) * 0.01)
            position_leg(rig, side, ankle[side] + ankle_offset, pitch)
            rotate_in_place(rig.pose.bones["UpperArm." + side], Quaternion((1, 0, 0), math.radians(swing)))
            bpy.context.view_layer.update()
            rotate_in_place(rig.pose.bones["LowerArm." + side], Quaternion((1, 0, 0), math.radians(min(0, swing * 0.38))))
            bpy.context.view_layer.update()
        for bone in rig.pose.bones:
            bone.rotation_mode = "QUATERNION"
            for property_name in ("location", "rotation_quaternion", "scale"):
                bone.keyframe_insert(property_name, frame=frame, group=bone.name)
    return action


def main():
    source = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
    destination = Path(__file__).resolve().parents[1] / "art" / "operator"
    destination.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = 24
    bpy.ops.import_scene.gltf(filepath=str(source), merge_vertices=True)
    rig = next(obj for obj in scene.objects if obj.type == "ARMATURE")
    meshes = [obj for obj in scene.objects if obj.type == "MESH" and obj.name.startswith("Worker_")]
    actions = {action.name.rsplit("|", 1)[-1]: action for action in bpy.data.actions}
    for track in list(rig.animation_data.nla_tracks):
        rig.animation_data.nla_tracks.remove(track)
    rig.name = "WorkerRig"
    matrix = rig.matrix_world.copy()
    rig.parent = None
    rig.matrix_world = matrix
    rig.data.pose_position = "REST"
    set_finishes()
    for mesh in meshes:
        refine_worker_part(mesh)
    bpy.ops.object.select_all(action="DESELECT")
    for mesh in meshes:
        mesh.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    mesh = bpy.context.view_layer.objects.active
    mesh.name = "Worker"
    rig.select_set(True)
    export_file(destination / "Worker.fbx", scene, rig)
    rig.data.pose_position = "POSE"
    idle = actions["Idle_Neutral"]
    jump = author_clip("Worker_Jump", [
        (0, -5, (3, 0, 0), (-5, 0, -7), 15, 11, -7),
        (3, 1, (13, 12, -9), (-10, 10, 8), -42, -26, 9),
        (8, 2, (25, 28, -6), (-12, 21, 12), -32, -15, 8),
        (13, 0, (20, 9, 3), (-9, 12, 11), -13, 2, 3),
        (18, -2, (10, 1, 2), (-7, 4, -2), 3, 9, -4),
    ], rig, idle)
    land = author_clip("Worker_Land", [
        (0, -2, (10, 0, 2), (-7, 0, 0), 3, 9, -4),
        (2, -15, (7, 0, 0), (-5, 0, 0), 20, 16, -13),
        (4, -8, (5, 0, 0), (-3, 0, 0), 8, 5, -6),
        (7, 0, (0, 0, 0), (0, 0, 0), 0, 0, 0),
    ], rig, idle)
    for name, action in {"Idle": idle, "Walk": actions["Walk"], "Run": actions["Run"], "Jump": jump, "Land": land}.items():
        export_file(destination / f"{name}.fbx", scene, rig, action)


if __name__ == "__main__":
    main()
