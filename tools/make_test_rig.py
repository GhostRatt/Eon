# Builds a stand-in skinned humanoid FBX (Mannequin-style bone names, breast/glute bones,
# one shape key) for testing the character pipeline. Run with Blender's Python:
#   python tools/make_test_rig.py out.fbx
import sys, math
import bpy
from mathutils import Vector

out = sys.argv[-1]
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'

def add(kind, loc, scale, rot=(0, 0, 0)):
    if kind == 'cyl':
        bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=1, depth=1, location=loc, rotation=rot)
    else:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, radius=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.ops.object.modifier_add(type='SUBSURF'); o.modifiers[-1].levels = 1
    bpy.ops.object.modifier_apply(modifier=o.modifiers[-1].name)
    return o

# Body (Z up in Blender, meters). Height ~1.70 m
parts = [
    add('sph', (0, 0, 0.98), (0.17, 0.11, 0.12)),    # pelvis
    add('sph', (0, 0, 1.18), (0.14, 0.10, 0.16)),    # waist
    add('sph', (0, 0, 1.38), (0.17, 0.11, 0.15)),    # chest
    add('sph', (0.075, -0.09, 1.34), (0.065, 0.06, 0.06)),   # breast L (front = -Y)
    add('sph', (-0.075, -0.09, 1.34), (0.065, 0.06, 0.06)),  # breast R
    add('sph', (0.08, 0.09, 0.93), (0.085, 0.07, 0.09)),     # glute L
    add('sph', (-0.08, 0.09, 0.93), (0.085, 0.07, 0.09)),    # glute R
    add('cyl', (0, 0, 1.55), (0.05, 0.05, 0.10)),    # neck
    add('sph', (0, 0, 1.66), (0.09, 0.10, 0.12)),    # head
]
for s in (1, -1):
    parts += [
        add('cyl', (s * 0.09, 0, 0.72), (0.075, 0.075, 0.42)),           # thigh
        add('cyl', (s * 0.09, 0, 0.30), (0.055, 0.055, 0.42)),           # calf
        add('sph', (s * 0.09, -0.06, 0.04), (0.05, 0.12, 0.04)),         # foot
        add('cyl', (s * 0.33, 0, 1.45), (0.04, 0.04, 0.28), (0, math.pi / 2, 0)),  # upper arm
        add('cyl', (s * 0.59, 0, 1.45), (0.035, 0.035, 0.26), (0, math.pi / 2, 0)), # forearm
        add('sph', (s * 0.77, 0, 1.45), (0.07, 0.02, 0.04)),             # hand
    ]
bpy.ops.object.select_all(action='DESELECT')
for p in parts: p.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
body = bpy.context.active_object
body.name = 'SK_TestBody'
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=0.002); bpy.ops.object.mode_set(mode='OBJECT')

mat = bpy.data.materials.new('M_Skin'); mat.diffuse_color = (0.85, 0.66, 0.55, 1)
body.data.materials.append(mat)

# Armature with UE Mannequin names
bpy.ops.object.armature_add(location=(0, 0, 0))
arm = bpy.context.active_object; arm.name = 'Armature'
bpy.ops.object.mode_set(mode='EDIT')
eb = arm.data.edit_bones
eb.remove(eb[0])
def bone(name, head, tail, parent=None):
    b = eb.new(name); b.head = Vector(head); b.tail = Vector(tail)
    if parent: b.parent = eb[parent]
    return b
bone('root', (0, 0, 0), (0, 0, 0.2))
bone('pelvis', (0, 0, 0.95), (0, 0, 1.05), 'root')
bone('spine_01', (0, 0, 1.05), (0, 0, 1.20), 'pelvis')
bone('spine_02', (0, 0, 1.20), (0, 0, 1.33), 'spine_01')
bone('spine_03', (0, 0, 1.33), (0, 0, 1.48), 'spine_02')
bone('neck_01', (0, 0, 1.50), (0, 0, 1.58), 'spine_03')
bone('head', (0, 0, 1.58), (0, 0, 1.78), 'neck_01')
for s, side in ((1, 'l'), (-1, 'r')):
    bone(f'breast_{side}', (s * 0.075, -0.04, 1.34), (s * 0.075, -0.14, 1.34), 'spine_03')
    bone(f'glute_{side}', (s * 0.08, 0.03, 0.95), (s * 0.08, 0.15, 0.92), 'pelvis')
    bone(f'clavicle_{side}', (s * 0.03, 0, 1.46), (s * 0.18, 0, 1.46), 'spine_03')
    bone(f'upperarm_{side}', (s * 0.19, 0, 1.45), (s * 0.46, 0, 1.45), f'clavicle_{side}')
    bone(f'lowerarm_{side}', (s * 0.46, 0, 1.45), (s * 0.72, 0, 1.45), f'upperarm_{side}')
    bone(f'hand_{side}', (s * 0.72, 0, 1.45), (s * 0.84, 0, 1.45), f'lowerarm_{side}')
    bone(f'thigh_{side}', (s * 0.09, 0, 0.93), (s * 0.09, 0, 0.51), 'pelvis')
    bone(f'calf_{side}', (s * 0.09, 0, 0.51), (s * 0.09, 0, 0.09), f'thigh_{side}')
    bone(f'foot_{side}', (s * 0.09, 0, 0.09), (s * 0.09, -0.14, 0.03), f'calf_{side}')
bpy.ops.object.mode_set(mode='OBJECT')

bpy.ops.object.select_all(action='DESELECT')
body.select_set(True); arm.select_set(True)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.parent_set(type='ARMATURE_AUTO')

# Shape key (like Primrose's outfit-fit morphs)
body.shape_key_add(name='Basis')
k = body.shape_key_add(name='Fit_Bra')
for i, v in enumerate(body.data.vertices):
    co = v.co
    if co.z > 1.25 and co.z < 1.45 and co.y < -0.05:
        k.data[i].co = co + Vector((0, 0.02, 0.01))

bpy.ops.export_scene.fbx(filepath=out, use_selection=False, object_types={'ARMATURE', 'MESH'},
                         add_leaf_bones=False, bake_anim=False, use_mesh_modifiers=False, mesh_smooth_type='FACE',
                         apply_scale_options='FBX_SCALE_UNITS', axis_forward='-Z', axis_up='Y')
print('wrote', out, len(body.data.vertices), 'verts')
