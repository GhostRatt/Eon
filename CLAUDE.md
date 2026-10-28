# Eon — notes for Claude

Kelvin's custom-engine sim: one AI agent, Jenny, living in a physical world (C++20, raylib render, Jolt physics).

## Read code through eongraph first (saves most of the tokens)
`python3 tools/codegraph/eongraph.py <cmd>` (on Windows, `python`). It re-indexes itself when files change.
- `context "<task words>"`: start every task here. It gives the relevant symbols grouped by file with lines, docs and notes (~350 tokens).
- `q "<cmd> <args>" "<cmd> <args>" ...`: batch several queries in ONE call. Prefer this to several separate calls.
- `brief <sym>`: signature, doc, notes, members/methods, callers and callees in one view.
- `show <sym> [<sym2> ...] [N]`: just those functions' source (N = extra context lines). Use it instead of `Read` on whole files.
- `file <path>` outline · `find <name>` · `refs <identifier>` (which functions use a member/variable) · `callers`/`callees <sym>`
- `impact <sym>` (what a change can break, incl. tests) · `path A B` · `diff [rev]` (symbols touched by uncommitted changes)
- `map` overview · `changed` · `report` (hubs, big functions, TODOs, notes)
- `note <sym> "<text>"`: save a durable gotcha/decision on a symbol (`tools/codegraph/notes.json`, committed). Add one whenever something took real effort to figure out.
- `eval`: search-quality benchmark (`tools/codegraph/eval.json`). `python3 tools/codegraph/test_eongraph.py` runs the regression checks.

Only `Read` a whole file when you're about to rewrite most of it. For edits, `show` the function, then `Edit` with the exact text.

## Build / test: use tools/dev.py (prints only what matters)
- `python3 tools/dev.py check [filter] [--apps]`: builds (errors only, trimmed), runs eon_tests (+ eongraph tests when unfiltered), and prints one summary line. Use it after every change.
- `python3 tools/dev.py test <filter>`: just the matching tests (`eon_tests --only X`, `--list`, `--times`).
- `python3 tools/dev.py shot NAME [zone.json] [sim args]` writes `out/shots/NAME.png`; `dev.py sheet OUT A B C [--cols 2] [--full]` makes a contact sheet with the HUD cropped. Read only the sheet, not every shot.
- `python3 tools/dev.py handoff`: a compact state of the work (commits, uncommitted symbols, notes, TODOs). Run it first after a context reset.
- `python3 tools/dev.py bench [--rig]`: CPU ms/frame per stage (motor, physics, hair, skinning, mind...). Run it before and after anything that could cost frame time.
- `python3 tools/dev.py win`: cross-compiles Windows .exe files (MinGW, `apt-get install mingw-w64`) into build-win/bin. Kelvin's PC folder is `E:\Eon-main\Eon-main` (a git clone): after pushing, `git fetch`+`merge --ff-only` there over the device bridge, and copy the .exe files into `build\bin\Release` with device_commit_files. His `BUILD.bat` writes `build_log.txt` (read it over the bridge when his build fails).
- `python3 tools/dev.py viz` regenerates the visual (`out/eon_codegraph.html`). Republish it to the "Eon Code Atlas" artifact when the code has changed a lot.

Raw commands, if needed:
- `cmake --build build` builds the tests and libraries (headless); `./build/bin/eon_tests` must stay at 0 failed.
- `cmake --build build-apps` builds `eon_sim` and `eon_editor` (raylib).
- Renders: `xvfb-run -a ./build-apps/bin/eon_sim content/zones/test_zone_01.json --spawn X Z YAWDEG --settle SECONDS --cam x y z tx ty tz --shot NAME.png`. The PNG lands in the cwd under its basename. Extras: `--stance N`, `--stance-at T N`, `--demo learn|switch|lever|crate`, `--show-mind`, `--mind-out DIR`, `--power`.
- The Primrose model lives outside git: `../Assets for Claude/RyanReos_PrimroseDeluxe_FullArchive/...` (symlinked in the container). Tests use `tests/data/test_rig.fbx`.
- Zones are generated: edit `tools/make_zones.py`, then `python3 tools/make_zones.py content/zones`.

## World content
- Zone entity types: static/dynamic boxes, `battery` `wire` (hidden = in-wall, no body) `lamp` `button` `switch` `lever` `motor` `resistor` `heater` `relay` `prefab` `door` (hinged panel: hingeSide, frame, swing, seal, friction, locked; must clear the floor), `ghost` param = no body (markers, air volumes; `airC` + `door` = fridge/oven air), matter: `tap` (valve=entity id, rate, speed, jets, spread) `container` (capacity, water) `bowl` (static container, drain=valve id) `food` (grams, kcalPerG, waterPerG, inside = flesh rgb; voxel block, bites leave marks).
- Outdoors: `terrain` (heightfield: res, seed, amp, feature, octaves, rough, basins [[x,z,r,depth]], heights = saved sculpt via `EncodeHeights`; `core/Terrain.h` has brushes) and `water` (box volume, top = surface; tempC; `autoLevel` fills the hollow it sits in). List terrain first. `onGround` param drops a thing onto the terrain. Zone `sun`/`sunDir` light it (sky background). `World::GroundHeight`, `WaterDepth`, `WaterTemp`, `TerrainField`/`TerrainChanged`. Dynamic boxes float/sink by density; containers held under fill; streams into water vanish; she gets wet as deep as she is, feels "in water"/"cold water"/"can't breathe", drinks with her mouth in it. Zone `outdoors_01.json`.
- Plants (`World_Plants.cpp`): `plant` (species grass|flower|bush|berry_bush|tree|fruit_tree, growth 0..1, moisture, variety) and `seed` (species). Grow in game days (zone `dayLength` real s/day, `growth` multiplier) with water (pond nearby = water table, puddles soak in) and light (sun, lamps); wilt/die dry, recover watered. Fruit trees/berry bushes grow `food` on point-joint stems: ripen green→red (3 days), pulled > ~6 N comes off, fall overripe, rot on the ground → seed → sprout on moist ground. Trees/bushes have trunk/bush bodies (role Terrain = not drawn as boxes); grass/flowers have no body. `ScatterPlants`, `PlantState`, `FruitRipeness`, `FruitOnPlant`, `GameDays`.
- Clothes (`engine/character/Dressing`): no undress command. Grip the waistband at her hips (or a bra band at her side) and move the hands along the garment's path (`Garment.off` 0..1, the mesh slides rigidly); fully off it's a `cloth` object in her hand; bottoms pushed to the ankles come off when she steps. Holding the cloth at her feet/chest and drawing it back puts it on. Wardrobe layer rules + streamer mode still apply; feelings "something snug on my hips", "cloth around my knees"... Sim `--demo undress`.
- Her body: stomach fills when she eats (`Swallow`, `Fullness`), empties over ~2 game h; food/drink water → bladder → pee. No bowel/stool (removed on purpose). Sim keys B bite, L pee; `--bite NAME` takes 3 operator bites (to see marks).
- Objects (prefabs): `content/objects/NAME.json` = parts + internal links (by part name). Zone entity `type: "prefab"`, `params.prefab`, optional `params.set {"Part.key": v}`. Expanded in `World::SetZone` (`ExpandPrefabs`): ids `instance*1000+n`, names `Inst.Part`, `"$Part"` → sibling name, `"#Part"` → sibling id, yaw rotates pos and axis/dir/hinge/out. Parts carry `params._from`; `ZoneToJson` saves only the instance. `SavePrefab`/`ListPrefabs` for tools.
- Scripts: Lua 5.4 per entity, `params.script` (file under content/) or `params.script_src`. Callbacks `on_start`, `on_tick(self,dt)` 30 Hz, `on_press`/`on_release`; `after`/`every` timers; `self:` param/set_param/closed/power/set_relay/set_tap/set_glow/set_color/push/water/food_left; doors: angle/shut/set_locked/locked; `world.` find/time/air_temp/agent_near/log/spawn/spawn_object(name,x,y,z,yaw,{set}) (`World::SpawnObject`). Anything spawned is removed by `Reset` and never saved. Sandboxed (no io/require). Errors land in `World::ScriptLog`, the script stops. A `relay` is a switch only scripts drive.
- Editor (`apps/editor`, Dear ImGui docking + rlImGui): `Editor.h` state; `main.cpp` (undo = zone JSON snapshots, open/save, place/delete/duplicate), `Panels.cpp` (menu, toolbar, outliner, schema-driven inspector, content browser, console, live schematic, World panel: map settings/terrain/plants, New map/Open/Save-as dialogs), `Tools.cpp` (viewport input: fly RMB+WASD, select, move gizmo, place, wire drag in-wall or loose rope + right-click cut, terrain brushes raise/lower/smooth/flatten/noise/pond, plant brush), `Plugins.cpp` (Lua plugins in content/plugins, API in content/plugins/README.md, hot reload). Part settings shown in the inspector come from `BuildSchemas` (add one there whenever an entity type learns a new param). New-map templates: `engine/core/MapGen` (`MakeMap`). Commit/Rebuild replace the entity vector: never hold Entity* across them. `--shot F.png --select NAME --tool wire|move|terrain|plants --cam ...` for screenshots.
- Operator test of fixtures: sim `--actuate NAME` (holds/flips it); in tests use `World::DebugActuate`, `ContainerMl`, `PuddleMl`, `AirTemperature`, `FoodLeftG`.

## Rules of the project
- Sensor firewall: Jenny's mind only gets `engine/mind/Perception` output. No entity names, types or ids ever reach `Learner`/`MemoryGraph`.
- She acts only through motor primitives (`MotorIntent`). There is no "use object" command; buttons, levers and wires are physical.
- Realism over graphics. Every new movement or interaction gets a test in `tests/test_main.cpp`.
- Commits end with the attribution lines from the session reminder; push to GhostRatt/Eon.
