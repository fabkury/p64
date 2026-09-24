"""Builds the p64b scene in Blender and renders the video frames (Cycles, GPU).

    blender -b -P scene.py -- [--start N] [--end N] [--step N] [--samples N] [--frames a,b,c] [--no-render]

Run from docs/video/ after pieces.scad has been exported into build/ and bake_leds.py has
written build/leds/. Imports the committed shell STL (enclosure/output/p64b/v7b/) and the
mock-up pieces, assembles them in the shell's design coordinates, stands the device on its
wedge foot, lights it in a dark studio, maps the baked LED frames onto the LED face, and
animates the camera and the explosion from storyboard.py. Frames land in build/frames/;
the scene is saved as build/p64b.blend so it can be opened and inspected.
"""

import json
import math
import os
import re
import sys

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import storyboard as sb  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD = os.path.join(HERE, "build")
SHELL_STL = os.path.join(ROOT, "enclosure", "output", "p64b", "v7b", "p64_enclosure_print.stl")
MM = 0.001


def args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = {"start": 1, "end": sb.LAST_RENDER_FRAME, "step": 1, "samples": 48, "frames": None, "render": True}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--start":
            out["start"] = int(argv[i + 1]); i += 1
        elif a == "--end":
            out["end"] = int(argv[i + 1]); i += 1
        elif a == "--step":
            out["step"] = int(argv[i + 1]); i += 1
        elif a == "--samples":
            out["samples"] = int(argv[i + 1]); i += 1
        elif a == "--frames":
            out["frames"] = [int(x) for x in argv[i + 1].split(",")]; i += 1
        elif a == "--no-render":
            out["render"] = False
        i += 1
    return out


def values():
    txt = open(os.path.join(BUILD, "values.echo"), encoding="utf-8").read()
    m = re.search(r'VALUES = "(\{.*\})"', txt)
    return json.loads(m.group(1))


# ---------------------------------------------------------------- materials
def principled(name, color, rough=0.5, metallic=0.0, coat=0.0, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Coat Weight"].default_value = coat
    b.inputs["Specular IOR Level"].default_value = spec
    return m


MATS = {}


def mats():
    MATS["shell"] = principled("pla_black", (0.018, 0.018, 0.02), rough=0.55, coat=0.15)
    MATS["insert"] = principled("pla_tomato", (0.65, 0.12, 0.06), rough=0.5)
    MATS["frame"] = principled("frame_plastic", (0.06, 0.06, 0.065), rough=0.6)
    MATS["board"] = principled("pcb_green", (0.02, 0.10, 0.05), rough=0.35, coat=0.4)
    MATS["mask"] = principled("led_mask", (0.01, 0.01, 0.012), rough=0.3, coat=0.6)
    MATS["chip_pcb"] = principled("pcb_blue", (0.02, 0.05, 0.16), rough=0.35, coat=0.4)
    MATS["enc_board"] = principled("pcb_dark", (0.02, 0.06, 0.04), rough=0.35, coat=0.4)
    MATS["metal"] = principled("steel", (0.75, 0.75, 0.77), rough=0.35, metallic=1.0)
    MATS["dark_metal"] = principled("dark_steel", (0.35, 0.35, 0.37), rough=0.4, metallic=1.0)
    MATS["alu"] = principled("alu_black", (0.10, 0.10, 0.11), rough=0.3, metallic=0.9)
    MATS["gold"] = principled("gold", (0.85, 0.62, 0.22), rough=0.3, metallic=1.0)
    MATS["white"] = principled("white_plastic", (0.55, 0.55, 0.55), rough=0.6)
    MATS["grey"] = principled("grey_plastic", (0.3, 0.3, 0.32), rough=0.5)
    MATS["black"] = principled("black_plastic", (0.02, 0.02, 0.02), rough=0.45)
    MATS["knob"] = principled("knob_black", (0.015, 0.015, 0.015), rough=0.35, coat=0.3)
    MATS["floor"] = principled("floor", (0.02, 0.02, 0.024), rough=0.42, coat=0.0, spec=0.3)


def led_face_material(frames_dir, mask_path):
    m = bpy.data.materials.new("led_face")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    mix = nt.nodes.new("ShaderNodeMixShader")
    body = nt.nodes.new("ShaderNodeBsdfPrincipled")
    body.inputs["Base Color"].default_value = (0.01, 0.01, 0.012, 1)
    body.inputs["Roughness"].default_value = 0.3
    body.inputs["Coat Weight"].default_value = 0.6
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Strength"].default_value = 9.0
    tex = nt.nodes.new("ShaderNodeTexImage")
    first = os.path.join(frames_dir, "led_0001.png")
    img = bpy.data.images.load(first)
    img.source = "SEQUENCE"
    tex.image = img
    tex.image_user.frame_duration = sb.FRAMES
    tex.image_user.frame_start = 1
    tex.image_user.frame_offset = 0
    tex.image_user.use_auto_refresh = True
    tex.image_user.use_cyclic = False
    tex.interpolation = "Closest"
    tex.extension = "CLIP"
    mask = nt.nodes.new("ShaderNodeTexImage")
    mask.image = bpy.data.images.load(mask_path)
    mask.image.colorspace_settings.name = "Non-Color"
    mask.interpolation = "Linear"
    uv = nt.nodes.new("ShaderNodeUVMap")
    nt.links.new(uv.outputs["UV"], tex.inputs["Vector"])
    nt.links.new(uv.outputs["UV"], mask.inputs["Vector"])
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    nt.links.new(mask.outputs["Color"], mix.inputs["Fac"])
    nt.links.new(body.outputs["BSDF"], mix.inputs[1])
    nt.links.new(emit.outputs["Emission"], mix.inputs[2])
    nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])
    return m


# ---------------------------------------------------------------- geometry
def import_stl(path, name, mat, parent, group):
    bpy.ops.wm.stl_import(filepath=path, global_scale=MM)
    ob = bpy.context.selected_objects[0]
    ob.name = name
    if any(abs(s - 1.0) > 1e-9 for s in ob.scale):        # the importer put the scale on the object: bake it into the mesh
        ob.data.transform(Matrix.Diagonal((*ob.scale, 1.0)))
        ob.scale = (1, 1, 1)
    else:
        ob.data.transform(Matrix.Scale(MM, 4)) if max(abs(c) for vt in ob.data.vertices[:50] for c in vt.co) > 1.0 else None
    ob.data.materials.append(mat)
    ob.parent = parent
    ob["group"] = group
    for p in ob.data.polygons:
        p.use_smooth = False
    return ob


def build(v):
    scene = bpy.context.scene
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)

    device = bpy.data.objects.new("device", None)
    scene.collection.objects.link(device)

    pieces = [
        # file, name, material, explosion group
        ("frame", "panel_frame", "frame", "panel"),
        ("board", "panel_board", "board", "panel"),
        ("mask", "panel_mask", "mask", "panel"),
        ("hub75", "panel_hub75", "black", "panel"),
        ("pwr", "panel_pwr", "white", "panel"),
        ("chip_pcb", "chip_pcb", "chip_pcb", "chip"),
        ("chip_usb", "chip_usb", "metal", "chip"),
        ("chip_module", "chip_module", "dark_metal", "chip"),
        ("chip_headers", "chip_headers", "white", "chip"),
        ("chip_socket", "chip_socket", "black", "chip"),
        ("ad_body", "ad_body", "alu", "adapters"),
        ("ad_plug", "ad_plug", "gold", "adapters"),
        ("insert", "insert", "insert", "insert"),
        ("enc_board", "enc_board", "enc_board", "encoders"),
        ("enc_sockets", "enc_sockets", "black", "encoders"),
        ("enc_body", "enc_body", "grey", "encoders"),
        ("enc_metal", "enc_metal", "metal", "encoders"),
        ("enc_nut", "enc_nut", "dark_metal", "enc_nut"),
        ("enc_knob", "enc_knob", "knob", "enc_knob"),
        ("screws", "screws", "dark_metal", "screws"),
    ]
    objs = []
    for f, name, mat, group in pieces:
        objs.append(import_stl(os.path.join(BUILD, f + ".stl"), name, MATS[mat], device, group))

    # the shell: the committed print STL, print_orient() undone -> design coordinates
    shell = import_stl(SHELL_STL, "shell", MATS["shell"], device, "shell")
    ba = math.radians(v["back_ang"])
    inv = Matrix.Rotation(-(math.pi + ba), 4, "X") @ Matrix.Translation((0, 0, -v["z_mid"] * math.cos(ba) * MM))
    shell.data.transform(inv)
    shell.data.update()
    objs.append(shell)
    for ax in range(3):
        vs = [vt.co[ax] / MM for vt in shell.data.vertices]
        print(f"shell in design coordinates: {'xyz'[ax]} {min(vs):.1f} .. {max(vs):.1f} mm")
    print("  (expected x -66.2 .. 66.2, y about -74.6 .. 64.6, z -13.5 .. 22.0)")

    # the LED face: a plane just in front of the mask, UVs so the picture reads upright from the front
    z0 = -(v["frame_d"] + v["panel_stack"]) * MM - 0.00005
    h = v["board"] / 2 * MM
    me = bpy.data.meshes.new("led_face")
    # seen from the front (looking towards +Z of the design frame) design +X is on the viewer's LEFT
    verts = [(h, -h, z0), (-h, -h, z0), (-h, h, z0), (h, h, z0)]   # bottom-left, bottom-right, top-right, top-left of the picture
    me.from_pydata(verts, [], [(0, 1, 2, 3)])
    me.update()
    uv = me.uv_layers.new(name="UVMap")
    for li, u in zip(range(4), [(0, 0), (1, 0), (1, 1), (0, 1)]):
        uv.data[li].uv = u
    face = bpy.data.objects.new("led_face", me)
    scene.collection.objects.link(face)
    face.parent = device
    face["group"] = "panel"
    mask_path = os.path.join(BUILD, "led_mask.png")      # written by bake_leds.py
    face.data.materials.append(led_face_material(os.path.join(BUILD, "leds"), mask_path))
    # normal must point to -Z (the front)
    if face.data.polygons[0].normal.z > 0:
        face.data.flip_normals()
    objs.append(face)

    # stand the device on its wedge foot: OpenSCAD's standing() = rotate([tilt]) rotate([90]) about X
    device.rotation_euler = (math.radians(90 + v["tilt"]), 0, 0)
    bpy.context.view_layer.update()
    zmin = min((shell.matrix_world @ Vector(c)).z for c in shell.bound_box)
    device.location = (0, 0, -zmin)
    bpy.context.view_layer.update()
    return device, objs, shell


def studio():
    scene = bpy.context.scene
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
    floor = bpy.context.object
    floor.name = "floor"
    floor.data.materials.append(MATS["floor"])

    def area(name, loc, target, power, size, color):
        d = bpy.data.lights.new(name, "AREA")
        d.energy = power
        d.size = size
        d.color = color
        ob = bpy.data.objects.new(name, d)
        scene.collection.objects.link(ob)
        ob.location = loc
        direction = Vector(target) - Vector(loc)
        ob.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        return ob

    c = (0, 0, 0.07)
    # area lights read as their radiance in glossy reflections: keep the power low for a dark studio
    area("key", (0.55, 0.75, 0.75), c, 28, 1.0, (1.0, 0.96, 0.9))
    area("fill", (-0.8, 0.5, 0.3), c, 7, 1.2, (0.9, 0.93, 1.0))
    area("rim", (-0.45, -0.7, 0.55), c, 22, 0.5, (0.8, 0.88, 1.0))
    area("back_key", (0.6, -0.8, 0.8), c, 24, 1.0, (1.0, 0.96, 0.9))
    area("top", (0, -0.1, 1.4), c, 6, 1.5, (1, 1, 1))

    world = bpy.data.worlds.new("studio")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.004, 0.004, 0.006, 1)
    bg.inputs["Strength"].default_value = 1.0


# ---------------------------------------------------------------- animation
def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * x * (x * (x * 6 - 15) + 10)


def lerp(a, b, k):
    return a + (b - a) * k


def camera_pose(t):
    beats = sb.CAMERA
    if t <= beats[0][0]:
        b = beats[0]
        return b[2:]
    for i in range(len(beats) - 1):
        a, b = beats[i], beats[i + 1]
        if a[1] <= t <= b[0]:
            k = smooth((t - a[1]) / (b[0] - a[1])) if b[0] > a[1] else 1.0
            return tuple(lerp(a[j], b[j], k) for j in range(2, 7))
    return beats[-1][2:]


def explode_k(t, order):
    s = order * sb.EXPLODE_STAGGER
    if t < sb.COLLAPSE_T0:
        return smooth((t - sb.EXPLODE_T0 - s) / (sb.EXPLODE_T1 - sb.EXPLODE_T0))
    return 1.0 - smooth((t - sb.COLLAPSE_T0 - s) / (sb.COLLAPSE_T1 - sb.COLLAPSE_T0))


def animate(device, objs, shell):
    scene = bpy.context.scene
    bpy.context.preferences.edit.keyframe_new_interpolation_type = "LINEAR"   # the easing is computed here, per frame
    cam_data = bpy.data.cameras.new("cam")
    cam_data.lens = 50
    cam_data.sensor_width = 36
    cam = bpy.data.objects.new("camera", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    target = bpy.data.objects.new("target", None)
    scene.collection.objects.link(target)
    con = cam.constraints.new("TRACK_TO")
    con.target = target
    con.track_axis = "TRACK_NEGATIVE_Z"
    con.up_axis = "UP_Y"

    axis = device.matrix_world.to_3x3() @ Vector((0, 0, 1))      # design +Z (into the shell) in world space
    axis.normalize()
    centre = sum((shell.matrix_world @ Vector(c) for c in shell.bound_box), Vector()) / 8
    groups = list(sb.EXPLODE_MM.keys())

    for f in range(1, sb.LAST_RENDER_FRAME + 1):
        t = (f - 1) / sb.FPS
        az, el, dist, tz, ty = camera_pose(t)
        tgt = Vector((centre.x, centre.y, tz)) + axis * ty
        target.location = tgt
        target.keyframe_insert("location", frame=f)
        a, e = math.radians(az), math.radians(el)
        cam.location = tgt + Vector((math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e))) * dist
        cam.keyframe_insert("location", frame=f)
        if f == 1 or f % 3 == 0 or sb.EXPLODE_T0 <= t <= sb.EXPLODE_T1 + 1 or sb.COLLAPSE_T0 <= t <= sb.COLLAPSE_T1 + 1:
            for ob in objs:
                g = ob["group"]
                mm = sb.EXPLODE_MM[g]
                if mm == 0.0:
                    continue
                k = explode_k(t, groups.index(g))
                ob.location = (0, 0, mm * MM * k)
                ob.keyframe_insert("location", frame=f)


# ---------------------------------------------------------------- render
def render_settings(samples):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    for kind in ("OPTIX", "CUDA"):
        try:
            devs = prefs.get_devices_for_type(kind)
        except Exception as e:
            print("cycles:", kind, "unavailable:", e)
            continue
        gpus = [d for d in devs if d.type != "CPU"]
        if gpus:
            prefs.compute_device_type = kind
            for d in devs:
                d.use = d.type != "CPU"        # the CPU listed next to the GPU only slows the frame down
            print("cycles: rendering on", kind, [d.name for d in gpus])
            break
    else:
        print("cycles: no GPU found, rendering on the CPU")
    scene.cycles.device = "GPU"
    scene.cycles.samples = samples
    scene.cycles.use_adaptive_sampling = True
    scene.cycles.adaptive_threshold = 0.03
    scene.cycles.use_denoising = True
    scene.cycles.denoiser = "OPENIMAGEDENOISE"
    scene.cycles.denoising_use_gpu = True
    scene.cycles.max_bounces = 6
    scene.cycles.caustics_reflective = False
    scene.cycles.caustics_refractive = False
    scene.render.resolution_x = sb.WIDTH
    scene.render.resolution_y = sb.HEIGHT
    scene.render.resolution_percentage = 100
    scene.render.fps = sb.FPS
    scene.frame_start = 1
    scene.frame_end = sb.LAST_RENDER_FRAME
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.filepath = os.path.join(BUILD, "frames", "f_")
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.render.film_transparent = False
    scene.render.use_persistent_data = True


def main():
    a = args()
    v = values()
    mats()
    device, objs, shell = build(v)
    studio()
    animate(device, objs, shell)
    render_settings(a["samples"])
    os.makedirs(os.path.join(BUILD, "frames"), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(BUILD, "p64b.blend"))
    if not a["render"]:
        return
    scene = bpy.context.scene
    if a["frames"]:
        for f in a["frames"]:
            scene.frame_set(f)
            scene.render.filepath = os.path.join(BUILD, "frames", f"f_{f:04d}.png")
            bpy.ops.render.render(write_still=True)
    else:
        scene.frame_start, scene.frame_end, scene.frame_step = a["start"], a["end"], a["step"]
        bpy.ops.render.render(animation=True)


main()
