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
  Jenny test keys: M cycle test motion (idle, bounce, walk in place, hands on chest/hips, arms crossed, turn), B bra support, C collision capsules, K skeleton.
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
| Jenny's real model | waiting for the Primrose FBX files (see below) |
| Mind bridge + sensor firewall, OBS | step 5 |
