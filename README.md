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
Dependencies (raylib 5.5, nlohmann/json) download automatically.

## Status
| Module | Status |
|---|---|
| `engine/core` Circuit | done — nodal solver: batteries (drain, sag, internal resistance), wires, switches, buttons, lamps, motors |
| `engine/core` Zone/World | done — JSON zones, fixed 120 Hz sim |
| `engine/render` | done — lamp-lit renderer, picking, fly camera |
| `apps/sim`, `apps/editor` | step 1 shells |
| Physics (Jolt), physical buttons, rope wires | step 2 |
| Editor tools | step 3 |
| Jenny (body, IK, soft tissue) | step 4 |
| Mind bridge + sensor firewall, OBS | step 5 |
