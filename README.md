# Eon

UE 5.8 AI-agent sim: Jenny, an AI who believes she is human, wakes in an unknown, growing world. Her body, needs and discoveries are simulated and logged; a drone camera streams her to OBS.

## Modules
| Folder | Status | Purpose |
|---|---|---|
| Source/Eon/Body | done | Core temp, sweat, shiver, needs, mood |
| Source/Eon/Log | done | JSONL research log in Saved/Eon/Logs |
| Source/Eon/Anim + EonAnimEditor | done | Limb collision (two-bone IK pushout), soft tissue springs, collider sets |
| Wardrobe / Censor | planned | Physical dress/undress, stream mode |
| Mind + brain/ | planned | Local VLM sidecar, memory |
| World / Drone | planned | PCG world, camera director, OBS out |

## Setup
1. Right-click `Eon.uproject` → Generate Visual Studio project files.
2. Build `EonEditor` (Development Editor) and open the project.
3. Follow `Docs/Step2_Primrose_Pipeline.md` for the character.
4. Add `EonBody` to the character BP; skin material needs a scalar `Wetness`.

Large assets use Git LFS (`git lfs install`).
