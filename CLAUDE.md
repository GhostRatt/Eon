# Eon — notes for Claude

Kelvin's custom-engine sim: one AI agent, Jenny, living in a physical world (C++20, raylib render, Jolt physics).

## Read code through eongraph first (saves most of the tokens)
`python3 tools/codegraph/eongraph.py <cmd>` (on Windows, `python`). It re-indexes itself when files change.
- `context "<task words>"`: start every task here. It lists the relevant symbols with file:lines and notes (~500–900 tokens).
- `show <Class::method>`: read just that function (with its leading comment), not the whole file.
- `file <path>`: a file's outline (signatures, docs, members), without bodies.
- `find <name>` · `callers <sym>` · `callees <sym>` · `impact <sym>` (what a change can break, incl. tests) · `path A B`
- `map`: whole-repo overview · `changed`: files changed since the last index · `report`: hubs, big functions, TODOs, notes
- `note <sym> "<text>"`: save a durable gotcha/decision on a symbol (`tools/codegraph/notes.json`, committed). Add one whenever something took real effort to figure out.

Only `Read` a whole file when you're about to rewrite most of it. For edits, `show` the function, then `Edit` with the exact text.

## Build / test
- `cmake --build build` builds the tests and libraries (headless); `./build/bin/eon_tests` must stay at 0 failed.
- `cmake --build build-apps` builds `eon_sim` and `eon_editor` (raylib).
- Renders: `xvfb-run -a ./build-apps/bin/eon_sim content/zones/test_zone_01.json --spawn X Z YAWDEG --settle SECONDS --cam x y z tx ty tz --shot NAME.png`. The PNG lands in the cwd under its basename. Extras: `--stance N`, `--stance-at T N`, `--demo learn|switch|lever|crate`, `--show-mind`, `--mind-out DIR`, `--power`.
- The Primrose model lives outside git: `../Assets for Claude/RyanReos_PrimroseDeluxe_FullArchive/...` (symlinked in the container). Tests use `tests/data/test_rig.fbx`.
- Zones are generated: edit `tools/make_zones.py`, then `python3 tools/make_zones.py content/zones`.

## Rules of the project
- Sensor firewall: Jenny's mind only gets `engine/mind/Perception` output. No entity names, types or ids ever reach `Learner`/`MemoryGraph`.
- She acts only through motor primitives (`MotorIntent`). There is no "use object" command; buttons, levers and wires are physical.
- Realism over graphics. Every new movement or interaction gets a test in `tests/test_main.cpp`.
- Commits end with the attribution lines from the session reminder; push to GhostRatt/Eon.
