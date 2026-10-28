#pragma once
// eon_editor: build maps for Jenny. Dear ImGui panels around a raylib viewport.
// Edit mode: physics frozen; everything you change goes into the zone (undoable) and is saved as JSON.
// Play mode: physics, circuits and scripts run so you can test; stopping restores the layout.
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "raylib.h"

#include "core/MapGen.h"
#include "core/World.h"
#include "render/ZoneRenderer.h"

struct lua_State;

namespace eon {

// One editable setting a part type understands (shown in the inspector even when not set yet).
struct ParamDef {
    std::string key, kind;   // kind: number | int | bool | text | vec3 | color | entity | choice
    nlohmann::json def;
    float lo = 0.f, hi = 0.f, step = 0.01f;
    std::vector<std::string> choices;
    std::string help;
};

// Something the content browser can place.
struct PaletteItem {
    std::string label, category, type;   // type: entity type, or "prefab"
    Vec3 size{0.2f, 0.2f, 0.2f};
    float mass = 0.f;
    Rgb color{170, 170, 175};
    nlohmann::json params = nlohmann::json::object();
    std::string help;
    std::string plugin;   // added by a plugin
};

enum class Tool { Select, Move, Wire, Terrain, Plants, Place };
enum class Brush { Raise, Lower, Smooth, Flatten, Noise, Water };

struct PluginTool {
    std::string plugin, name, help;
    int ref = 0;   // Lua function
};

struct Editor {
    // world
    std::string root;            // content folder
    std::string path;            // current zone file
    World world;
    ZoneRenderer renderer;
    RenderFrame frame;
    bool playing = false, dirty = false, light = true;

    // camera / viewport
    FlyCamera fly;
    RenderTexture2D view{};
    int viewW = 0, viewH = 0;
    Vector2 viewPos{0, 0};       // screen position of the viewport image
    bool viewHovered = false, viewFocused = false, looking = false;
    Ray mouseRay{};
    RayHit hit;
    bool hasHit = false;
    Vec3 aim;                    // world point under the mouse

    // selection / tools
    std::set<int> selection;
    int primary = -1;
    Tool tool = Tool::Select;
    float snap = 0.05f, rotSnap = 15.f;
    bool snapOn = true;
    int gizmoAxis = -1;          // dragging this axis (0 x, 1 y, 2 z)
    Vec3 gizmoStart, gizmoGrab;
    std::map<int, Vec3> dragStartPos;
    PaletteItem placing;
    bool placingActive = false;

    // wires
    int wireFromEntity = 0, wireFromTerm = -1;
    bool wireLoose = false;      // false: in-wall (ideal link), true: a real wire with clips she can pull off
    float wireGauge = 0.02f;     // ohm per metre
    Rgb wireColor{180, 60, 40};

    // terrain / plants
    Brush brush = Brush::Raise;
    float brushR = 2.f, brushStrength = 0.6f, flattenY = 0.f;
    bool stroking = false;
    int plantSpecies = 0;
    int plantDensity = 8;
    uint32_t scatterSeed = 1;

    // undo
    std::vector<std::string> undo, redo;
    std::string lastSnapshot;

    // content
    std::vector<PaletteItem> palette;
    std::vector<PrefabInfo> objects;
    std::vector<std::pair<std::string, std::string>> scripts;   // rel path, first comment line
    std::vector<std::string> zones;
    std::map<std::string, std::vector<ParamDef>> schemas;       // entity type -> settings
    std::string status;
    std::vector<std::string> console;

    // plugins (Lua)
    lua_State* L = nullptr;
    std::vector<std::string> plugins;
    std::vector<PluginTool> pluginTools;
    double pluginStamp = 0.0;

    // panels
    bool showOutliner = true, showInspector = true, showContent = true, showConsole = true, showSchematic = true, showTerrain = true;
    bool showNewMap = false, showOpen = false, showSaveAs = false, showDemo = false;
    char filter[64] = {};
    float scriptWatch = 0.f;
    long long scriptStamp = 0;

    // ---- helpers (main.cpp) ----
    Zone& Z() { return world.GetZone(); }
    Entity* Sel() { return primary > 0 ? Z().Find(primary) : nullptr; }
    void Log(const std::string& s);
    void Rebuild();                                       // zone -> runtime (after edits)
    void Commit(const std::string& what);                 // push an undo step + rebuild
    void Undo();
    void Redo();
    bool Open(const std::string& file);
    bool Save(const std::string& file);
    void RefreshContent();
    int Place(const PaletteItem& item, const Vec3& at);
    void DeleteSelection();
    void Duplicate();
    Entity* InstanceOf(const Entity* e);
    bool Electrical(const Entity& e) const;
};

// Panels.cpp
void BuildSchemas(Editor& ed);
void BuildPalette(Editor& ed);
void DrawMenuBar(Editor& ed);
void DrawToolbar(Editor& ed);
void DrawOutliner(Editor& ed);
void DrawInspector(Editor& ed);
void DrawContent(Editor& ed);
void DrawConsole(Editor& ed);
void DrawSchematic(Editor& ed);
void DrawTerrainPanel(Editor& ed);
void DrawDialogs(Editor& ed);
// Tools.cpp
void UpdateViewport(Editor& ed, float dt);   // input in the 3D view
void DrawViewportOverlay(Editor& ed);        // inside BeginMode3D
// Plugins.cpp
void LoadPlugins(Editor& ed);
void UnloadPlugins(Editor& ed);
void RunPluginTool(Editor& ed, const PluginTool& t);
void WatchPlugins(Editor& ed);
// MapGen (engine/core/MapGen.h): NewMapSpec, MakeMap

} // namespace eon
