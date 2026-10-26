# Eon Engine — design

Realism big, graphics small. Jenny learns the world the way a person would: by looking, touching and trying.

## Two programs
| Program | Who uses it | What it does |
|---|---|---|
| `eon_editor` | Kelvin | Build zones, place parts, wire circuits, tune physics, play-in-editor, inspect logs |
| `eon_sim` | Jenny | Runs her life: **Waiting Area** (persistent home: bed, water, toilet, light) + **Test Zones** (experiment rooms loaded/swapped while she is in the Waiting Area) |

Both share `eon_core` (scene, physics, electricity, body) so a zone behaves identically in both.

## Sensor firewall (she must not know the internals)
Everything the engine knows (entity names, types, parameters, IDs, circuit topology) stays on the engine side. The mind only receives what a human body would:
- **Vision:** rendered pixels from her eye cameras. No labels, no outlines, no UI.
- **Touch:** contact location on her body, pressure, temperature, texture class (smooth/rough/soft) — never the object's identity.
- **Proprioception / interoception:** joint angles, balance, effort, EonBody state (hot, thirsty, tired...).
- **Hearing:** audio cues (click, hum, buzz) as sound features, not names.

Her **actions are motor primitives only**: walk/turn, look at a point, reach to a point, grip/release, push/pull with force, sit/lie, say (inner voice). There is no "press button" or "use battery" command. If she pushes where a button is, the button physically moves; if the contacts close, current flows. She discovers that herself.

The firewall is enforced in code: the mind bridge (step 5) can only read from `SensorFrame` structs, which contain no engine IDs or strings from scene data.

## Real-world rules
- **Physics:** Jolt rigid bodies with real mass (kg), friction, restitution; gravity 9.81 m/s². Units: meters, kilograms, seconds.
- **Electricity:** nodal circuit solver every physics tick. Parts have two physical terminals. Current only flows where conductive things physically touch (wire clip on terminal, button contacts closed).
  - Battery: voltage, internal resistance, capacity (Ah) — drains, sags, dies; short circuits get hot.
  - Wire: resistance per meter, physical rope.
  - Button/switch: spring-loaded plunger; contacts close past travel threshold.
  - Lamp: brightness and heat from power.
  - Motor: torque from current, back-EMF from speed.
  - Later: diode/LED, fuse, relay, heater, fan, door actuator, sensor plates.
- **No magic:** nothing activates without a physical cause chain.

## Zones
`content/zones/*.json` — entities (internal name, part type, transform, size, mass, params) and terminal connections. The editor writes them; the sim loads them.

## Roadmap
1. Core + electrical solver + zone files + program shells
2. Jolt physics, physical buttons, rope wires, circuit↔physics coupling
3. Editor tools: place/move/rotate, wire tool, part library, save/load, play-in-editor
4. Jenny: FBX import, skinning, IK, soft tissue, limb collision, EonBody, motor primitives
5. Mind bridge + sensor firewall, Python VLM brain, research log, drone cams → OBS, censor
