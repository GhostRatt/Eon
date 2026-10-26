# Step 2 — Primrose → UE 5.8: rig, IK, collision, soft tissue

Model facts (from the readme): 183k tris, centimeters, 173 cm with heels, skinned in Max, Blender 3.0 file rigged with **Auto-Rig Pro**, ARKit facial blendshapes, clothing fitting morphs per layer.

## 1. Export from Blender (recommended over the Max FBX)
Auto-Rig Pro can export with UE5 Mannequin bone names, which makes retargeting near-automatic.

1. Open the `.blend` in Blender (3.6+ fine; upgrade ARP if prompted).
2. Check the armature for soft-tissue bones (breast / glute). If there are none:
   - Add one bone per breast (from `spine_04/05` forward to the nipple area) and one per glute (from `pelvis` back), parented to those spine/pelvis bones.
   - Weight paint a soft falloff (center ~1.0, fading to 0 at the edges); keep the skin attached to spine/pelvis at the borders.
3. ARP → Export → **Unreal Engine**:
   - Humanoid, **UE Mannequin names ON**, root motion as needed.
   - Include the custom soft bones (ARP: mark them as "custom bones" so they export).
   - Shape keys ON (outfit fit morphs + ARKit face).
   - Export the nude body as `SK_Jenny_Body`, and each clothing item separately **on the same armature** (`SK_Jenny_Bra`, `SK_Jenny_Panties`, `SK_Jenny_Shirt`...).

## 2. Import into UE 5.8
- Body: Skeletal Mesh, create skeleton `SKEL_Jenny`, Import Morph Targets ON, Normals: Import Normals and Tangents, units cm (no rescale).
- Clothes: same importer, **Skeleton = SKEL_Jenny**.
- Textures: TGA, normal maps already DirectX (no green flip).
- Skin material: add scalar `Wetness` (drives roughness down + slight darken) for EonBody sweat.

## 3. IK (everything)
1. **IK Rig** `IK_Jenny`: chains spine, neck, head, arm_l/r, leg_l/r, fingers. Goals: `hand_l`, `hand_r`, `foot_l`, `foot_r`, `head`. Full Body IK solver, pelvis as root.
2. **IK Retargeter** from UE5 Manny → Jenny. Source animations: **Game Animation Sample** (motion matching locomotion, traversal) + any Manny-compatible packs.
3. AnimBP (`ABP_Jenny`) graph order:
   ```
   Locomotion (motion matching / state machine)
     → Foot Placement (feet on ground, pelvis adjust)
     → Control Rig (hand reach/grab targets, look-at)   ← used by Wardrobe in step 3
     → Eon Limb Collision
     → Eon Soft Tissue
     → Output Pose
   ```
   Collision runs after all IK so nothing can push a hand back into the body; soft tissue runs last so it reacts to the final pose and to hand contact.
4. Clothing meshes: set **Leader Pose Component** to the body mesh. They inherit IK, collision and jiggle for free.

## 4. Collider set (hands never phase into the body)
Content Browser → Miscellaneous → Data Asset → `EonColliderSet` → `DA_Jenny_Colliders`. Starting capsules (tune with Draw Debug on):

| Bone | Tag | Radius | HalfHeight | Notes |
|---|---|---|---|---|
| pelvis | Pelvis | 13 | 6 | Rotation so axis runs left↔right |
| spine_02 | Torso | 11 | 5 | waist |
| spine_04 | Torso | 12 | 6 | ribcage |
| spine_05 | Torso | 11 | 5 | upper chest |
| neck_01 | Neck | 5 | 3 | |
| head | Head | 9 | 2 | |
| thigh_l / thigh_r | Thigh | 8 | 16 | Offset along bone to mid-thigh |
| breast_l / breast_r | Breast | 5 | 0 | smaller than the real shape so hands press slightly in |
| glute_l / glute_r | Glute | 6 | 0 | |
| hand_l / hand_r | Hand | 3.5 | 5 | pushers for soft tissue |
| lowerarm_l / lowerarm_r | Forearm | 3 | 10 | pushers for soft tissue |

**Eon Limb Collision** node:
- ColliderSet = `DA_Jenny_Colliders`
- Limbs: `upperarm_l → lowerarm_l → hand_l` and `upperarm_r → lowerarm_r → hand_r`
- Legs (optional): `thigh_l → calf_l → foot_l` with IgnoreTags = `Thigh`, `Pelvis`, `Glute`
- SurfaceOffset 0.2 (use −0.5 to let palms sink slightly into soft areas)

## 5. Soft tissue (real-world motion)
**Eon Soft Tissue** node. Each bone is a damped spring mass in world space, so it lags, overshoots and settles from every step, jump, turn and stop, sags under gravity, and is pushed by her hands.

Starting values (real tissue is underdamped: visible follow-through, 1–3 small rebounds):

| Group | Freq Hz | Damping | Sag cm | MaxOffset (x,y,z) | Length | TransScale | RotScale | TouchRadius |
|---|---|---|---|---|---|---|---|---|
| Breast | 2.8 | 0.28 | 0.8 | 2.5, 2.5, 3.5 | 8 | 0.4 | 0.7 | 6 |
| Glute | 4.0 | 0.40 | 0.3 | 1.5, 1.5, 2.0 | 7 | 0.5 | 0.5 | 7 |
| Belly (opt) | 5.0 | 0.50 | 0.2 | 0.8, 0.8, 0.8 | 6 | 0.6 | 0.2 | 8 |
| Thigh (opt) | 6.0 | 0.55 | 0.1 | 0.6, 0.6, 0.8 | 6 | 0.6 | 0.2 | 7 |

- **LocalForward** must point from the bone into the tissue (turn on Draw Debug: the magenta line should exit the front of the breast / back of the glute).
- **MaxOffset** is per local bone axis; give the vertical axis the most range.
- **Support** pin (array of Group + 0..1) — set from the Wardrobe: none 0, bra 0.6–0.7, sports bra 0.9, panties/shorts on Glute 0.3–0.5. Support stiffens, damps and reduces range and sag.
- **Intensity** pin: 1 awake, ~0.3 asleep.
- Only one simulated bone per tissue region (don't add parent and child of the same chain).

## 6. Test checklist
- Walk → run → sudden stop: tissue follows through and settles in ~0.5–1 s.
- Jump: visible lift on the way up, compression on landing.
- Hand on hip / crossed arms / hand on chest: hands rest on the skin, no clipping; soft areas compress under the hand.
- Teleport the actor: no explosion (auto reset).
- Clothes with Leader Pose move with the jiggle.

## Later polish
- Corrective morphs (shoulder, hip, elbow) driven by pose drivers.
- Chaos Cloth on loose garments, using the body's physics asset.
- ARKit blendshapes → Live Link Face / mood-driven expressions.
