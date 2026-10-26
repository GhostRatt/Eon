# Eon

UE 5.8 AI-agent sim: Jenny, an AI who believes she is human, wakes in an unknown, growing world. Her body, needs and discoveries are simulated and logged; a drone camera streams her to OBS.

## Modules
| Folder | Status | Purpose |
|---|---|---|
| Source/Eon/Body | done | Core temp, sweat, shiver, needs, mood |
| Source/Eon/Log | done | JSONL research log in Saved/Eon/Logs |
| Wardrobe / Censor | planned | Physical dress/undress, stream mode |
| Mind + brain/ | planned | Local VLM sidecar, memory |
| World / Drone | planned | PCG world, camera director, OBS out |

## Setup
1. Create a blank C++ UE 5.8 project named `Eon` in this folder (module name must be `Eon`).
2. Regenerate project files and build.
3. Add `EonBody` to the character BP; skin material needs a scalar `Wetness`.

Large assets use Git LFS (`git lfs install`).
