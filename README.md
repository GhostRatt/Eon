# Eon

A custom engine for one AI agent. Jenny, who believes she is human, lives in a **Waiting Area** and is sent into **Test Zones** full of real-world things (batteries, wires, buttons, lamps, motors). She knows nothing about the engine: she sees, touches and experiments to figure everything out. Realism big, graphics small.

Design: [`Docs/Engine_Design.md`](Docs/Engine_Design.md). The old Unreal Engine 5.8 version lives on the `ue5` branch.

## Programs
| Program | Purpose |
|---|---|
| `eon_sim` | Runs Jenny's world (Waiting Area + Test Zones) |
| `eon_editor` | Kelvin's tools: build and edit zones |
| `eon_tests` | Core tests (electricity, zone files) |

## Build (Windows 11, Visual Studio 2022 + CMake)
```
cmake -S . -B build
cmake --build build --config Release
build\bin\Release\eon_sim.exe
build\bin\Release\eon_editor.exe
build\bin\Release\eon_tests.exe
```
Dependencies (raylib 5.5, Jolt Physics 5.6, nlohmann/json) download automatically.

### Jenny's model
Put the Primrose files in `content/characters/jenny/source/` (git-ignored, they stay on your PC) and point
`content/characters/jenny/character.json` at the FBX. Until then the sim shows a stand-in rig built by
`tools/make_test_rig.py` (run with Blender's Python). `eon_sim --fbx path/to/model.fbx` loads any FBX directly.

### Controls
- **Sim:** RMB look, WASD/QE fly, LMB grab and carry (wheel = reach), F push with ~4 N, 1/2 switch zones, R reset, H HUD.
  Jenny test keys: M cycle test motion (idle, bounce, walk in place, hands on chest/hips, arms crossed, turn), 3-8 put on / take off garments,
  T room temperature (zone / 8°C / 38°C), Y game time x60, P streamer mode, C collision capsules, K skeleton.
  Motor tests (what her mind will do later): middle-click the floor to walk there, J / U reach right / left hand to the cursor,
  G close/open the reaching hand, V lift it 25 cm, X relax both arms, arrows move her (relative to your camera),
  Z cycle stance (Backspace = stand), hold Space to climb onto/off what's in front of her, O make her talk, N show her mind (memory context), F9 write her memory (html/report/json).
  Render scripts: `--stance-at T S` (switch stance at time T), `--demo switch|lever|crate` (scripted hand use in Test Zone 01).
- **Editor:** EDIT mode — LMB select, arrows/PgUp/PgDn move, R/F rotate, N new box, Ctrl+D duplicate, Del delete, Ctrl+S save. P toggles PLAY mode (physics on, grab/push/Space to test); stopping restores the layout.

## Status
| Module | Status |
|---|---|
| `engine/core` Circuit | done — nodal solver: batteries (drain, sag, internal resistance), wires, switches, buttons, lamps, motors |
| `engine/core` Zone/World | done — JSON zones, fixed 120 Hz sim |
| `engine/render` | done — lamp-lit renderer, picking, fly camera |
| `engine/core` Physics/World | done — Jolt 5.6: real mass/friction, spring buttons, bistable rocker switches, rope wires with clips that latch on release and pull off past 12 N, touching terminals conduct, kinematic grab hand with grip slip |
| `apps/sim`, `apps/editor` | operator hand (grab/carry, push); editor edit/play modes, terminal markers |
| Editor tools | step 3 |
| `engine/character` | done (verified on a stand-in rig) — FBX import via ufbx (skin weights, morphs, materials, any unit/axis → meters Y-up), auto rig mapping (Mannequin/ARP/Mixamo/Blender names), auto-facing, two-bone IK, skin-fitted body capsules, limb collision, breast/glute spring-mass soft tissue with support, multithreaded CPU skinning |
| `engine/render/CharacterRenderer` | done — textured (TGA/PNG/JPG), lamp-lit skin shading, 16-bit chunking for big meshes |
| Jenny (Primrose) | loaded from `E:\Eon-main\Assets for Claude` via `character.json`: textures, hair opacity, 77 morphs |
| `engine/character/Face` | blinking (random, tired = slow/heavy lids), eye saccades, mood/cold/heat expressions on ARKit morphs |
| `engine/character/Dangles` | hair + earring bone chains (33 chains / 129 bones): gravity, swing, styled shape, head/torso collision |
| `engine/character/BodyState` | needs, core temp, sweat → skin sheen, shiver tremor, breathing rate/depth, heart rate, mood, body-sense text |
| `engine/character/Motor` | her motor primitives (walk direction, face, look, per-hand reach/grip/effort): real torso + hand bodies in Jolt, walls stop her, light things get shoved, hands are muscle-driven (60 N max) so they get blocked by surfaces and physically push buttons, grips slip past 120 N, bends at the hips to reach across a table, planted-foot gait with hip bob and arm swing |
| Stances | stand, crouch (duck-walk), kneel, sit on the floor, sit on a seat (chair/bed edge/box — only if something is behind her), hands-and-knees crawl, prone (army crawl), lie on back / left side / right side (bed or floor); she moves through the in-between positions, and rolls over back ↔ side ↔ front keeping her head in place |
| Hands | 15 finger joints per hand found by name; fingers curl loosely when relaxed, open and spread when reaching, close around what she holds (fist on nothing), lie flat and spread when planted; palms turn toward what she reaches for, flat on the floor when planted; torso pitches until planted hands actually reach the floor |
| Using things | fingertip pointing (a small physical fingertip presses rockers/small buttons), whole-hand presses, gripping and throwing a wall lever along its arc, one- or two-handed carrying with targets that move with her body, shared weight between hands, wrists hold carried things steady, loads slow her down, hands draw back after letting go |
| Getting into bed | sit on the edge → lie down along it on her side (hips shuffle onto it, legs swing up) → roll → sit up over the edge she faces → stand on the floor |
| Floor contact | feet keep their orientation under hip lean (flat in a crouch); foot skin samples are pushed above the floor when kneeling/lying (tops of the feet flat); hair lies on the floor/bed instead of going through; breasts and glutes flatten and spread against the floor/bed they're pressed into |
| Feet & ground | barefoot `FlatFeets` + floor refit so soles sit flat; per-foot ground contact and normals; steps up/down to 38 cm; stops at drops |
| Climbing | Space with a direction: hands on the edge, knee up, stand on top (30–135 cm); climbs down off ledges too |
| `engine/character/LipSync` | text → visemes (or TTS phoneme timings, or live amplitude) on ARKit mouth morphs with coarticulation |
| Sleep | lying still while tired → drowsy → asleep (eyes shut, slow breathing, recovers fatigue); wakes rested or when cold/hot/needing the toilet |
| `engine/character/Wardrobe` | garments on her skeleton, layer rules (outer off first), bra/panty support for soft tissue, fit morphs, clo, streamer mode keeps underwear on |
| `engine/mind` (memory graph) | sensor firewall (`Perception`: eyes from her head with occlusion + peripheral vision, hands, body feelings; opaque tracks only), `Learner` (her own names for things, on/near, weight, movable, glow; cause→effect from timing with observed/inferred/ambiguous provenance and evidence counts), `MemoryGraph` (traversal-based context within a token budget, questions → curiosity, communities, sleep consolidation, journaled persistence, HTML viewer). See Docs/Mind_Memory.md |
| Mind bridge (LLM brain), OBS | next |

## Code graph (for working on Eon with an AI assistant)
`tools/codegraph/eongraph.py` maps the repo into a graph of symbols, calls and includes, built locally with tree-sitter (`pip install tree-sitter tree-sitter-cpp tree-sitter-python`; it falls back to regex without it).
`context "<task>"`, `show <sym>`, `impact <sym>` and similar commands return only the code that matters, 10–100× fewer tokens than opening files.
Notes on symbols live in `tools/codegraph/notes.json`. Assistant instructions are in `CLAUDE.md`.

