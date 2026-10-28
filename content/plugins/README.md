# Editor plugins

Each folder here with a `plugin.lua` is loaded by `eon_editor` at start, and reloaded whenever a `.lua` file in
here is saved. Plugins run inside the editor only (never in Jenny's world): they add things to build with.

## API (`eon.*`)
| call | what it does |
|---|---|
| `eon.register_part{label, category, type, size={x,y,z}, mass, color={r,g,b}, params={...}, help}` | a button in the Content panel (category = tab) |
| `eon.register_tool{name, help, run=function(ctx) ... end}` | an entry in the Plugins menu; `ctx.cursor = {x,y,z}` (where the mouse is in the view), `ctx.selection = {ids}`; everything it does is one undo step |
| `eon.register_schema(type, {{key, kind, default, min, max, step, choices, help}, ...})` | settings shown in the Inspector for that entity type (kind: number, int, bool, text, vec3, choice, entity, entityname) |
| `eon.add{type, name, pos, rot, size, mass, color, params}` → id | put something in the map |
| `eon.link(idA, terminalA, idB, terminalB)` | wire two terminals (in the wall) |
| `eon.get(id)` → table, `eon.set(id, {fields})`, `eon.remove(id)` | read / change / delete |
| `eon.selection()`, `eon.select{ids}`, `eon.find(name)`, `eon.entities(type)` | selection and lookup |
| `eon.cursor()` → x, y, z; `eon.ground(x, z)` → terrain height or nil | where things go |
| `eon.log(text)` (also `print`) | the Console panel |

Examples: `fences/` (fence ring, fence between two things), `circuits/` (pre-wired lamp kit, doorbell, AA battery part),
`furniture/` (table and chairs, bed, chair).
