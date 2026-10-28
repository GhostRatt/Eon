// Editor panels: menu, toolbar, outliner, inspector (schema-driven), content browser, console, schematic,
// world/terrain panel, dialogs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "raylib.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "rlImGui.h"
#include "raymath.h"

#include "Editor.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace eon {

namespace {
ParamDef Num(const char* k, double d, float lo, float hi, float step, const char* help) { ParamDef p; p.key = k; p.kind = "number"; p.def = d; p.lo = lo; p.hi = hi; p.step = step; p.help = help; return p; }
ParamDef Int(const char* k, int d, int lo, int hi, const char* help) { ParamDef p; p.key = k; p.kind = "int"; p.def = d; p.lo = (float)lo; p.hi = (float)hi; p.help = help; return p; }
ParamDef Bool(const char* k, bool d, const char* help) { ParamDef p; p.key = k; p.kind = "bool"; p.def = d; p.help = help; return p; }
ParamDef Vec(const char* k, Vec3 d, const char* help) { ParamDef p; p.key = k; p.kind = "vec3"; p.def = json::array({d.x, d.y, d.z}); p.help = help; return p; }
ParamDef Ent(const char* k, const char* help) { ParamDef p; p.key = k; p.kind = "entity"; p.def = 0; p.help = help; return p; }
ParamDef Name(const char* k, const char* help) { ParamDef p; p.key = k; p.kind = "entityname"; p.def = ""; p.help = help; return p; }
ParamDef Choice(const char* k, const char* d, std::vector<std::string> c, const char* help) { ParamDef p; p.key = k; p.kind = "choice"; p.def = d; p.choices = std::move(c); p.help = help; return p; }
ParamDef Text(const char* k, const char* d, const char* help) { ParamDef p; p.key = k; p.kind = "text"; p.def = d; p.help = help; return p; }

const char* kTypes[] = {"static", "dynamic", "door", "battery", "lamp", "button", "switch", "lever", "relay", "motor", "resistor", "heater", "wire",
                        "tap", "container", "bowl", "food", "seed", "plant", "terrain", "water", "prefab"};

void HelpMark(const std::string& s) {
    if (s.empty()) return;
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f); ImGui::TextUnformatted(s.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
}

ImU32 Col(Rgb c, float a = 1.f) { return IM_COL32(c.r, c.g, c.b, static_cast<int>(a * 255)); }
}   // namespace

void BuildSchemas(Editor& ed) {
    auto& S = ed.schemas;
    S["static"] = {Choice("material", "", {"", "soft", "metal"}, "Surface: soft (bed, cushion) is grippy and dead; metal is slippery."),
                   Bool("onGround", false, "Outdoors: sit on the terrain wherever it's put."),
                   Bool("ghost", false, "No body at all: a marker, or an air volume (with airC)."),
                   Num("airC", 4, -20, 250, 0.5f, "Air volume (ghost): its own air temperature while its door is shut (fridge 4, oven 200)."),
                   Name("door", "The door that closes this air volume.")};
    S["dynamic"] = {Choice("material", "", {"", "soft", "metal"}, "Surface: soft or metal."), Bool("onGround", false, "Outdoors: dropped onto the terrain.")};
    S["door"] = {Choice("hingeSide", "left", {"left", "right"}, "Which vertical edge the hinge is on (seen from the side it opens toward, local +z)."),
                 Name("frame", "What it hangs on (empty = the world)."), Num("swing", 100, 10, 180, 1, "How far it opens, degrees."),
                 Num("seal", 2, 0, 30, 0.1f, "Latch / magnetic seal: N·m pulling the last few degrees shut."),
                 Num("friction", 0.5f, 0, 10, 0.05f, "Hinge friction, N·m."), Bool("locked", false, "Starts bolted.")};
    S["battery"] = {Num("voltage", 9, 0.1f, 400, 0.1f, "Open-circuit volts."), Num("internalR", 1.5f, 0.001f, 100, 0.01f, "Internal resistance (ohm): sag under load."),
                    Num("capacityAh", 0.5f, 0.01f, 500, 0.01f, "Charge it holds."), Num("charge", 1, 0, 1, 0.01f, "How full it starts (0..1)."),
                    Bool("hidden", false, "Inside a wall: electrical only, not visible.")};
    S["lamp"] = {Num("ratedPower", 5, 0.1f, 500, 0.1f, "Watts at rated voltage."), Num("ratedVoltage", 12, 0.5f, 400, 0.1f, "Its design voltage (brightness follows power).")};
    S["button"] = {Vec("axis", {0, 1, 0}, "Which way the cap faces (its push goes the other way)."), Num("travel", 0.008f, 0.001f, 0.05f, 0.001f, "How far it goes in, m."),
                   Num("force", 2.5f, 0.2f, 50, 0.1f, "Spring force to press it, N.")};
    S["switch"] = {Vec("axis", {0, 1, 0}, "Face direction of the plate."), Vec("hinge", {0, 0, 1}, "Rocker hinge axis."), Num("throw", 0.3f, 0.05f, 1.2f, 0.01f, "Rocker angle each way, rad."),
                   Num("detent", 0.015f, 0, 2, 0.005f, "Snap force holding it on either side."), Bool("closed", false, "Starts on.")};
    S["lever"] = {Vec("axis", {0, 1, 0}, "Direction the handle sticks out of the plate."), Num("handle", 0.3f, 0.05f, 1.2f, 0.01f, "Handle length, m."),
                  Num("throw", 0.6f, 0.1f, 1.5f, 0.01f, "Angle each way, rad."), Num("detent", 1, 0, 20, 0.05f, "How firmly it stays up/down."),
                  Num("leverMass", 0.4f, 0.02f, 5, 0.01f, "Handle mass, kg."), Bool("closed", false, "Starts up (on).")};
    S["relay"] = {Bool("closed", false, "Starts closed. Scripts switch it (self:set_relay).")};
    S["motor"] = {Num("resistance", 2, 0.05f, 100, 0.05f, "Winding resistance, ohm."), Num("k", 0.01f, 0.0001f, 1, 0.0005f, "Torque/back-EMF constant.")};
    S["resistor"] = {Num("resistance", 100, 0.01f, 1e6f, 1, "Ohm.")};
    S["heater"] = {Num("ratedPower", 120, 1, 3000, 1, "Watts at rated voltage: warms the air around it."), Num("ratedVoltage", 12, 1, 400, 0.5f, "Design voltage.")};
    S["wire"] = {Num("length", 0.4f, 0.02f, 50, 0.01f, "Length, m (resistance follows)."), Num("resistance", 0.008f, 0, 10, 0.001f, "Ohm (default 0.02 per m)."),
                 Bool("hidden", false, "In a wall: electrical link only, no physical wire.")};
    S["tap"] = {Ent("valve", "Entity that opens it: a button (held), switch or lever. Or set the text \"script\"."), Num("rate", 60, 0.5f, 2000, 0.5f, "Flow ml/s."),
                Num("speed", 0.6f, 0.05f, 5, 0.01f, "Water speed out of it, m/s."), Int("jets", 1, 1, 64, "Spray holes (shower head)."),
                Num("spread", 0, 0, 1, 0.01f, "Jet spread."), Vec("dir", {0, -1, 0}, "Spout direction (local)."), Bool("powered", false, "Needs current through it (a pump).")};
    S["container"] = {Num("capacity", 300, 5, 100000, 1, "ml."), Num("water", 0, 0, 100000, 1, "ml in it at the start."), Ent("drain", "Valve that empties it.")};
    S["bowl"] = {Num("capacity", 3000, 5, 200000, 10, "ml."), Num("water", 0, 0, 200000, 10, "ml at the start."), Ent("drain", "Flush lever / plug: empties it while open.")};
    S["food"] = {Num("grams", 150, 1, 5000, 1, "Weight of food in it."), Num("kcalPerG", 0.52f, 0, 9, 0.01f, "Energy (apple 0.52, bread 2.6)."),
                 Num("waterPerG", 0.85f, 0, 1, 0.01f, "Water share (goes to her bladder)."), Vec("inside", {240, 232, 190}, "Flesh colour (r g b) shown where it's bitten.")};
    S["seed"] = {Choice("species", "flower", {"grass", "flower", "bush", "berry_bush", "tree", "fruit_tree"}, "What grows from it (on moist ground).")};
    S["plant"] = {Choice("species", "grass", {"grass", "flower", "bush", "berry_bush", "tree", "fruit_tree"}, "Kind of plant."), Num("growth", 1, 0.02f, 1, 0.01f, "How grown (0..1)."),
                  Num("moisture", 0.6f, 0, 1, 0.01f, "Soil water at the start."), Int("variety", 0, 0, 4, "Flower colour / look variant."), Num("health", 1, 0, 1, 0.01f, "")};
    S["terrain"] = {Int("res", 128, 8, 1024, "Height samples per side (multiple of 4)."), Int("seed", 1, 0, 1000000, "Random seed for the hills."),
                    Num("amp", 2, 0, 50, 0.05f, "Hill height, m."), Num("feature", 12, 1, 400, 0.5f, "Size of the biggest hills, m."), Int("octaves", 5, 1, 8, "Detail layers."),
                    Num("rough", 0.5f, 0, 1, 0.01f, "How much detail each layer keeps.")};
    S["water"] = {Num("tempC", 16, -2, 60, 0.5f, "Water temperature."), Bool("autoLevel", false, "Fill the hollow it sits in (surface just below the rim)."),
                  Num("freeboard", 0.12f, 0, 2, 0.01f, "With autoLevel: how far below the rim.")};
    S["prefab"] = {Text("prefab", "", "Object file in content/objects.")};
    for (auto& [type, defs] : S) {   // every part can carry a script and be a part of the electrical "hidden" layer
        (void)type;
    }
}

void BuildPalette(Editor& ed) {
    std::vector<PaletteItem> keep;
    for (const PaletteItem& p : ed.palette) if (!p.plugin.empty()) keep.push_back(p);
    auto& P = ed.palette;
    P.clear();
    auto add = [&](const char* cat, const char* label, const char* type, Vec3 size, float mass, Rgb c, json params, const char* help) {
        PaletteItem it;
        it.category = cat; it.label = label; it.type = type; it.size = size; it.mass = mass; it.color = c; it.params = std::move(params); it.help = help;
        P.push_back(it);
    };
    add("Build", "Wall", "static", {2.f, 2.6f, 0.1f}, 0, {205, 200, 190}, json::object(), "A wall section (2 m).");
    add("Build", "Floor tile", "static", {2.f, 0.1f, 2.f}, 0, {150, 140, 128}, json::object(), "A floor slab.");
    add("Build", "Block", "static", {0.4f, 0.4f, 0.4f}, 0, {150, 150, 155}, json::object(), "A fixed box: table, shelf, step...");
    add("Build", "Table", "static", {1.0f, 0.74f, 0.6f}, 0, {140, 100, 70}, json::object(), "Table height block.");
    add("Build", "Crate", "dynamic", {0.4f, 0.3f, 0.3f}, 4.f, {150, 110, 70}, json::object(), "A loose box she can push or carry (4 kg).");
    add("Build", "Small box", "dynamic", {0.2f, 0.12f, 0.15f}, 0.5f, {200, 190, 170}, json::object(), "Light loose box.");
    add("Build", "Door", "door", {0.8f, 1.95f, 0.04f}, 15.f, {150, 110, 80}, {{"hingeSide", "left"}, {"swing", 100}, {"seal", 2}}, "Hinged door with a latch.");
    add("Build", "Marker", "static", {0.05f, 0.05f, 0.05f}, 0, {255, 0, 255}, {{"ghost", true}}, "Invisible point scripts can refer to.");
    add("Electric", "Battery 9V", "battery", {0.026f, 0.048f, 0.017f}, 0.045f, {60, 60, 60}, {{"voltage", 9}, {"internalR", 1.5}, {"capacityAh", 0.5}}, "Loose 9 V battery.");
    add("Electric", "Battery 12V", "battery", {0.06f, 0.15f, 0.1f}, 0, {60, 60, 60}, {{"voltage", 12}, {"internalR", 0.05}, {"capacityAh", 7}}, "Fixed 12 V battery.");
    add("Electric", "Lamp", "lamp", {0.06f, 0.1f, 0.06f}, 0, {250, 240, 200}, {{"ratedPower", 5}, {"ratedVoltage", 12}}, "Lights up with current.");
    add("Electric", "Button", "button", {0.05f, 0.05f, 0.02f}, 0, {240, 240, 240}, {{"axis", json::array({0, 0, 1})}}, "Push button (closed while held).");
    add("Electric", "Switch", "switch", {0.02f, 0.12f, 0.08f}, 0, {245, 245, 245}, {{"axis", json::array({1, 0, 0})}}, "Rocker switch on a plate.");
    add("Electric", "Lever", "lever", {0.04f, 0.16f, 0.1f}, 0, {90, 90, 95}, {{"axis", json::array({1, 0, 0})}, {"handle", 0.3}, {"throw", 0.6}, {"detent", 1}}, "Wall lever.");
    add("Electric", "Relay", "relay", {0.05f, 0.05f, 0.05f}, 0, {230, 230, 230}, {{"script", "scripts/timer_light.lua"}}, "Switch that only a script drives.");
    add("Electric", "Motor", "motor", {0.06f, 0.06f, 0.08f}, 0, {120, 120, 130}, {{"resistance", 2}, {"k", 0.01}}, "DC motor.");
    add("Electric", "Resistor", "resistor", {0.03f, 0.01f, 0.01f}, 0, {200, 170, 120}, {{"resistance", 100}}, "");
    add("Electric", "Heater", "heater", {0.45f, 0.45f, 0.12f}, 0, {150, 110, 90}, {{"ratedPower", 150}, {"ratedVoltage", 12}}, "Warms the air around it.");
    add("Water", "Tap", "tap", {0.03f, 0.02f, 0.03f}, 0, {200, 210, 220}, {{"valve", "script"}, {"rate", 45}, {"speed", 0.3}}, "Water outlet; set its valve.");
    add("Water", "Shower head", "tap", {0.12f, 0.03f, 0.12f}, 0, {190, 190, 195}, {{"valve", "script"}, {"rate", 120}, {"speed", 0.4}, {"jets", 9}, {"spread", 0.35}}, "");
    add("Water", "Cup", "container", {0.075f, 0.1f, 0.075f}, 0.15f, {235, 235, 230}, {{"capacity", 300}}, "");
    add("Water", "Bucket", "container", {0.25f, 0.28f, 0.25f}, 0.6f, {90, 120, 160}, {{"capacity", 8000}}, "");
    add("Water", "Basin", "bowl", {0.5f, 0.15f, 0.4f}, 0, {240, 240, 240}, {{"capacity", 6000}}, "Fixed bowl (sink, toilet...).");
    add("Water", "Pond", "water", {6.f, 2.f, 6.f}, 0, {70, 110, 140}, {{"autoLevel", true}, {"tempC", 15}}, "Water volume (fills the hollow).");
    add("Food", "Apple", "food", {0.075f, 0.075f, 0.075f}, 0.15f, {190, 40, 40}, {{"grams", 150}, {"kcalPerG", 0.52}, {"waterPerG", 0.85}}, "");
    add("Food", "Bread", "food", {0.12f, 0.05f, 0.08f}, 0.08f, {200, 160, 100}, {{"grams", 80}, {"kcalPerG", 2.6}, {"waterPerG", 0.35}}, "");
    add("Nature", "Seed", "seed", {0.012f, 0.008f, 0.012f}, 0.001f, {70, 50, 30}, {{"species", "flower"}}, "Sprouts on moist ground.");
    for (const char* sp : {"grass", "flower", "bush", "berry_bush", "tree", "fruit_tree"})
        add("Nature", sp, "plant", {0.1f, 0.1f, 0.1f}, 0, {80, 140, 50}, {{"species", sp}, {"growth", 1.0}}, "");
    add("Nature", "Rock", "static", {0.7f, 0.5f, 0.6f}, 0, {128, 124, 116}, {{"onGround", true}}, "");
    for (const PrefabInfo& o : ed.objects) {
        PaletteItem it;
        it.category = "Objects"; it.label = o.name; it.type = "prefab"; it.size = {0.1f, 0.1f, 0.1f}; it.params = {{"prefab", o.name}}; it.help = o.description;
        P.push_back(it);
    }
    for (const PaletteItem& p : keep) P.push_back(p);
}

// ---------------------------------------------------------------------------------------------------------------
void DrawMenuBar(Editor& ed) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New map...", "Ctrl+N")) ed.showNewMap = true;
        if (ImGui::MenuItem("Open...", "Ctrl+O")) { ed.RefreshContent(); ed.showOpen = true; }
        if (ImGui::MenuItem("Save", "Ctrl+S")) ed.Save(ed.path);
        if (ImGui::MenuItem("Save as...")) ed.showSaveAs = true;
        if (ImGui::MenuItem("Reload from disk")) ed.Open(ed.path);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) CloseWindow();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ed.undo.empty())) ed.Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !ed.redo.empty())) ed.Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !ed.selection.empty())) ed.Duplicate();
        if (ImGui::MenuItem("Delete", "Del", false, !ed.selection.empty())) ed.DeleteSelection();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) { ed.selection.clear(); for (const Entity& e : ed.Z().entities) if (!e.params.contains("_from")) ed.selection.insert(e.id); }
        if (ImGui::MenuItem("Clear selection", "Esc")) { ed.selection.clear(); ed.primary = -1; }
        ImGui::Separator();
        ImGui::MenuItem("Snap to grid", nullptr, &ed.snapOn);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Outliner", nullptr, &ed.showOutliner);
        ImGui::MenuItem("Inspector", nullptr, &ed.showInspector);
        ImGui::MenuItem("Content", nullptr, &ed.showContent);
        ImGui::MenuItem("World / terrain", nullptr, &ed.showTerrain);
        ImGui::MenuItem("Schematic", nullptr, &ed.showSchematic);
        ImGui::MenuItem("Console", nullptr, &ed.showConsole);
        ImGui::Separator();
        if (ImGui::MenuItem("Editor light", nullptr, &ed.light)) ed.renderer.ambient = ed.light ? 0.6f : 0.1f;
        if (ImGui::MenuItem("Reset layout")) { ImGui::ClearIniSettings(); std::error_code ec; fs::remove("eon_editor_layout.ini", ec); }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Play")) {
        if (ImGui::MenuItem(ed.playing ? "Stop" : "Play", "P")) {
            ed.playing = !ed.playing;
            ed.world.EndGrab();
            ed.Rebuild();
            ed.Log(ed.playing ? "Playing: physics, circuits and scripts run" : "Stopped: layout restored");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Plugins")) {
        for (const PluginTool& t : ed.pluginTools) {
            if (ImGui::MenuItem((t.plugin + ": " + t.name).c_str())) RunPluginTool(ed, t);
            if (!t.help.empty() && ImGui::BeginItemTooltip()) { ImGui::TextUnformatted(t.help.c_str()); ImGui::EndTooltip(); }
        }
        if (ed.pluginTools.empty()) ImGui::TextDisabled("(no plugin tools)");
        ImGui::Separator();
        if (ImGui::MenuItem("Reload plugins")) { UnloadPlugins(ed); LoadPlugins(ed); }
        if (ImGui::MenuItem("Open plugins folder")) OpenURL(fs::absolute(ed.root + "/plugins").string().c_str());
        ImGui::Separator();
        for (const std::string& p : ed.plugins) ImGui::TextDisabled("%s", p.c_str());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::TextUnformatted("Viewport: RMB + WASD/QE fly (Shift fast), LMB select (Ctrl add)");
        ImGui::TextUnformatted("Tools: 1 select  2 move  3 wire  4 terrain  5 plants   Esc: back to select");
        ImGui::TextUnformatted("Move: drag the red/green/blue arrows; Shift = no snap");
        ImGui::TextUnformatted("Wire: drag from a green terminal to another (right-click a wire to cut it)");
        ImGui::TextUnformatted("Ctrl+Z / Ctrl+Y undo/redo, Ctrl+D duplicate, Del delete, Ctrl+S save, P play");
        ImGui::Separator();
        ImGui::MenuItem("ImGui demo (widgets reference)", nullptr, &ed.showDemo);
        ImGui::EndMenu();
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 360);
    ImGui::TextDisabled("%s%s", fs::path(ed.path).filename().string().c_str(), ed.dirty ? "  *unsaved*" : "");
    ImGui::EndMainMenuBar();
}

void DrawToolbar(Editor& ed) {
    const float h = ImGui::GetFrameHeight() + 10.f;
    if (!ImGui::BeginViewportSideBar("##toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, h, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }
    if (ImGui::BeginMenuBar()) {
        auto toolBtn = [&](const char* label, Tool t, const char* tip) {
            const bool on = ed.tool == t;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label)) { ed.tool = t; ed.placingActive = false; ed.wireFromEntity = 0; }
            if (on) ImGui::PopStyleColor();
            if (ImGui::BeginItemTooltip()) { ImGui::TextUnformatted(tip); ImGui::EndTooltip(); }
            ImGui::SameLine();
        };
        toolBtn("Select (1)", Tool::Select, "Click things to select them (Ctrl+click adds).");
        toolBtn("Move (2)", Tool::Move, "Drag the arrows to move the selection (snaps to the grid).");
        toolBtn("Wire (3)", Tool::Wire, "Drag from one terminal to another to connect them. Right-click a wire to cut it.");
        toolBtn("Terrain (4)", Tool::Terrain, "Sculpt the ground (brush settings in the World panel).");
        toolBtn("Plants (5)", Tool::Plants, "Plant (LMB) or clear (Shift+LMB) plants.");
        ImGui::Separator();
        ImGui::Checkbox("Snap", &ed.snapOn);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::DragFloat("##snap", &ed.snap, 0.005f, 0.005f, 1.f, "%.3f m");
        if (ed.tool == Tool::Wire) {
            ImGui::Separator();
            ImGui::TextUnformatted("New wires:");
            ImGui::SameLine();
            if (ImGui::RadioButton("in the wall", !ed.wireLoose)) ed.wireLoose = false;
            ImGui::SameLine();
            if (ImGui::RadioButton("loose wire", ed.wireLoose)) ed.wireLoose = true;
            if (ImGui::BeginItemTooltip()) { ImGui::TextUnformatted("A real wire with clips at both ends: it hangs, she can pull the clips off."); ImGui::EndTooltip(); }
        }
        if (ed.tool == Tool::Terrain || ed.tool == Tool::Plants) {
            ImGui::Separator();
            ImGui::SetNextItemWidth(110);
            ImGui::SliderFloat("radius", &ed.brushR, 0.2f, 20.f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        }
        ImGui::Separator();
        if (ImGui::Button(ed.playing ? "Stop (P)" : "Play (P)")) {
            ed.playing = !ed.playing;
            ed.world.EndGrab();
            ed.Rebuild();
            ed.Log(ed.playing ? "Playing: physics, circuits and scripts run" : "Stopped: layout restored");
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------
void DrawOutliner(Editor& ed) {
    if (!ImGui::Begin("Outliner", &ed.showOutliner)) { ImGui::End(); return; }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter by name or type", ed.filter, sizeof(ed.filter));
    const std::string f = ed.filter;
    auto match = [&](const Entity& e) { return f.empty() || e.name.find(f) != std::string::npos || e.type.find(f) != std::string::npos; };
    int breakApart = 0, action = 0;
    auto row = [&](const Entity& e, bool part) {
        ImGui::PushID(e.id);
        const bool sel = ed.selection.count(e.id) > 0;
        char label[160];
        std::snprintf(label, sizeof(label), "%s%s", part ? "   " : "", e.name.c_str());
        if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
            if (ImGui::GetIO().KeyCtrl) { if (sel) ed.selection.erase(e.id); else ed.selection.insert(e.id); }
            else ed.selection = {e.id};
            ed.primary = e.id;
            if (ImGui::IsMouseDoubleClicked(0)) {
                const Vec3 to = e.pos;
                const Vector3 d = Vector3Subtract(ed.fly.cam.target, ed.fly.cam.position);
                ed.fly.cam.target = {to.x, to.y, to.z};
                const float len = std::max(1.5f, std::max({e.size.x, e.size.y, e.size.z}) * 2.5f);
                ed.fly.cam.position = Vector3Subtract(ed.fly.cam.target, Vector3Scale(Vector3Normalize(d), len));
            }
        }
        if (ImGui::BeginPopupContextItem()) {
            if (!ed.selection.count(e.id)) { ed.selection = {e.id}; ed.primary = e.id; }
            // (changes to the list are done after drawing it)
            if (ImGui::MenuItem("Duplicate")) action = 1;
            if (ImGui::MenuItem("Delete")) action = 2;
            if (e.type == "prefab" && ImGui::MenuItem("Break object apart")) breakApart = e.id;
            ImGui::EndPopup();
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.f);
        ImGui::TextDisabled("%s", e.type.c_str());
        ImGui::PopID();
    };
    // grouped: plants and grass collapse (outdoor maps have hundreds)
    int plants = 0;
    if (ImGui::BeginChild("##list")) {
        for (const Entity& e : ed.Z().entities) {
            if (e.params.contains("_from") || e.params.value("spawned", false)) continue;
            if (e.type == "plant") { ++plants; continue; }
            if (!match(e)) continue;
            row(e, false);
            if (e.type == "prefab")
                for (const Entity& p : ed.Z().entities)
                    if (p.params.value("_from", std::string()) == e.name && match(p)) row(p, true);
        }
        if (plants && ImGui::TreeNode("plants", "Plants (%d)", plants)) {
            for (const Entity& e : ed.Z().entities) if (e.type == "plant" && match(e)) row(e, false);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    if (action == 1) ed.Duplicate();
    if (action == 2) ed.DeleteSelection();
    if (breakApart) {
        // its parts become ordinary, editable things
        if (const Entity* inst = ed.Z().Find(breakApart)) {
            const std::string n = inst->name;
            for (Entity& x : ed.Z().entities) if (x.params.value("_from", std::string()) == n) x.params.erase("_from");
            auto& E = ed.Z().entities;
            E.erase(std::remove_if(E.begin(), E.end(), [&](const Entity& x) { return x.id == breakApart; }), E.end());
            ed.selection.clear();
            ed.primary = -1;
            ed.Commit("Broke " + n + " apart");
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------
namespace {
// One setting: returns true when it changed (value written into params)
bool ParamWidget(Editor& ed, json& params, const ParamDef& d, bool& done) {
    const bool set = params.contains(d.key);
    const json v = set ? params[d.key] : d.def;
    bool changed = false;
    ImGui::PushID(d.key.c_str());
    if (!set) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::SetNextItemWidth(-90);
    const char* lab = d.key.c_str();
    if (d.kind == "number") {
        float x = v.is_number() ? v.get<float>() : 0.f;
        if (ImGui::DragFloat(lab, &x, d.step, d.lo, d.hi, "%.4g")) { params[d.key] = x; changed = true; }
    } else if (d.kind == "int") {
        int x = v.is_number() ? v.get<int>() : 0;
        if (ImGui::DragInt(lab, &x, 0.2f, (int)d.lo, (int)d.hi)) { params[d.key] = x; changed = true; }
    } else if (d.kind == "bool") {
        bool x = v.is_boolean() ? v.get<bool>() : false;
        if (ImGui::Checkbox(lab, &x)) { params[d.key] = x; changed = true; }
    } else if (d.kind == "vec3") {
        float x[3] = {0, 0, 0};
        if (v.is_array() && v.size() == 3) for (int i = 0; i < 3; ++i) x[i] = v[i].get<float>();
        if (ImGui::DragFloat3(lab, x, 0.01f)) { params[d.key] = json::array({x[0], x[1], x[2]}); changed = true; }
    } else if (d.kind == "choice") {
        const std::string cur = v.is_string() ? v.get<std::string>() : "";
        if (ImGui::BeginCombo(lab, cur.empty() ? "(default)" : cur.c_str())) {
            for (const std::string& c : d.choices)
                if (ImGui::Selectable(c.empty() ? "(default)" : c.c_str(), c == cur)) { params[d.key] = c; changed = true; }
            ImGui::EndCombo();
        }
    } else if (d.kind == "entity" || d.kind == "entityname") {
        // pick another thing in the map
        std::string cur;
        if (d.kind == "entity") {
            if (v.is_number_integer()) { const Entity* t = ed.Z().Find(v.get<int>()); cur = t ? t->name : (v.get<int>() ? "#" + std::to_string(v.get<int>()) : ""); }
            else if (v.is_string()) cur = v.get<std::string>();
        } else cur = v.is_string() ? v.get<std::string>() : "";
        if (ImGui::BeginCombo(lab, cur.empty() ? "(none)" : cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (ImGui::Selectable("(none)", cur.empty())) { params.erase(d.key); changed = true; }
            if (d.kind == "entity" && ImGui::Selectable("script", cur == "script")) { params[d.key] = "script"; changed = true; }
            for (const Entity& e : ed.Z().entities) {
                if (e.type == "plant") continue;
                if (ImGui::Selectable((e.name + "  (" + e.type + ")").c_str(), e.name == cur)) {
                    if (d.kind == "entity") params[d.key] = e.id; else params[d.key] = e.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        std::string x = v.is_string() ? v.get<std::string>() : v.dump();
        if (ImGui::InputText(lab, &x)) { params[d.key] = x; changed = true; }
    }
    done |= ImGui::IsItemDeactivatedAfterEdit() || (changed && (d.kind == "bool" || d.kind == "choice" || d.kind == "entity" || d.kind == "entityname"));
    if (!set) ImGui::PopStyleColor();
    HelpMark(d.help + (set ? "" : "  (not set: default shown)"));
    if (set) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { params.erase(d.key); changed = true; done = true; }
        if (ImGui::BeginItemTooltip()) { ImGui::TextUnformatted("Back to the default"); ImGui::EndTooltip(); }
    }
    ImGui::PopID();
    return changed;
}
}   // namespace

void DrawInspector(Editor& ed) {
    if (!ImGui::Begin("Inspector", &ed.showInspector)) { ImGui::End(); return; }
    Entity* e = ed.Sel();
    if (!e) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextWrapped("Click something in the viewport or the outliner. Ctrl+click to select several (then Duplicate, Delete, or Save as object in Content).");
        ImGui::End();
        return;
    }
    if (ed.selection.size() > 1) ImGui::TextDisabled("%d selected (editing the last clicked)", static_cast<int>(ed.selection.size()));
    Entity* inst = ed.InstanceOf(e);
    bool live = false, done = false;
    int cutLink = -1;
    ImGui::Text("#%d", e->id);
    ImGui::SameLine();
    std::string name = e->name;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && !inst) { e->name = name; done = true; }
    if (inst) {
        ImGui::TextColored({1.f, 0.7f, 0.3f, 1.f}, "Part of object \"%s\" (%s)", inst->name.c_str(), inst->params.value("prefab", std::string()).c_str());
        ImGui::TextWrapped("Settings you change here are saved as overrides on that object. Select the object to move it.");
        if (ImGui::Button("Select the object")) { ed.selection = {inst->id}; ed.primary = inst->id; ImGui::End(); return; }
    }
    // type
    ImGui::SetNextItemWidth(-90);
    if (ImGui::BeginCombo("type", e->type.c_str())) {
        for (const char* t : kTypes) if (ImGui::Selectable(t, e->type == t) && !inst) { e->type = t; done = true; }
        ImGui::EndCombo();
    }
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) && !inst) {
        float p[3] = {e->pos.x, e->pos.y, e->pos.z}, r[3] = {e->rotDeg.x, e->rotDeg.y, e->rotDeg.z}, s[3] = {e->size.x, e->size.y, e->size.z};
        if (ImGui::DragFloat3("position", p, 0.01f, 0, 0, "%.3f")) { e->pos = {p[0], p[1], p[2]}; live = true; }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::DragFloat3("rotation", r, 1.f, -360, 360, "%.0f°")) { e->rotDeg = {r[0], r[1], r[2]}; live = true; }
        done |= ImGui::IsItemDeactivatedAfterEdit();
        if (e->type != "prefab" && e->type != "plant") {
            if (ImGui::DragFloat3("size", s, 0.005f, 0.001f, 500, "%.3f")) { e->size = {std::max(0.001f, s[0]), std::max(0.001f, s[1]), std::max(0.001f, s[2])}; live = true; }
            done |= ImGui::IsItemDeactivatedAfterEdit();
            float m = e->massKg;
            if (ImGui::DragFloat("mass kg (0 = fixed)", &m, 0.01f, 0, 5000, "%.3f")) { e->massKg = std::max(0.f, m); live = true; }
            done |= ImGui::IsItemDeactivatedAfterEdit();
            float c[3] = {e->color.r / 255.f, e->color.g / 255.f, e->color.b / 255.f};
            if (ImGui::ColorEdit3("colour", c)) { e->color = {(unsigned char)(c[0] * 255), (unsigned char)(c[1] * 255), (unsigned char)(c[2] * 255)}; live = true; }
            done |= ImGui::IsItemDeactivatedAfterEdit();
        }
    }
    // settings this kind of thing understands
    json& target = inst ? inst->params["set"] : e->params;
    if (inst && !target.is_object()) target = json::object();
    json view = json::object();   // for parts: the part's own params with the overrides on top
    if (inst) { view = e->params; for (auto it = target.begin(); it != target.end(); ++it) { const std::string k = it.key(); const std::string pre = e->name.substr(inst->name.size() + 1) + "."; if (k.rfind(pre, 0) == 0) view[k.substr(pre.size())] = it.value(); } }
    json& params = inst ? view : e->params;
    auto it = ed.schemas.find(e->type);
    bool paramChanged = false;
    if (ImGui::CollapsingHeader("How it works", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (it != ed.schemas.end()) for (const ParamDef& d : it->second) paramChanged |= ParamWidget(ed, params, d, done);
        else ImGui::TextDisabled("(no settings known for this type)");
        // anything else set on it
        std::vector<std::string> extra;
        for (auto p = params.begin(); p != params.end(); ++p) {
            const std::string k = p.key();
            if (k == "_from" || k == "script" || k == "script_src" || k == "set" || k == "spawned" || k == "heights") continue;
            bool known = false;
            if (it != ed.schemas.end()) for (const ParamDef& d : it->second) known |= d.key == k;
            if (!known) extra.push_back(k);
        }
        if (!extra.empty()) ImGui::SeparatorText("other settings");
        for (const std::string& k : extra) {
            ParamDef d;
            d.key = k;
            const json& v = params[k];
            d.kind = v.is_boolean() ? "bool" : v.is_number_integer() ? "int" : v.is_number() ? "number" : (v.is_array() && v.size() == 3) ? "vec3" : "text";
            d.lo = -1e9f; d.hi = 1e9f; d.step = 0.01f;
            d.def = v;
            paramChanged |= ParamWidget(ed, params, d, done);
        }
        static std::string newKey, newVal;
        ImGui::SetNextItemWidth(100);
        ImGui::InputTextWithHint("##nk", "new setting", &newKey);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-60);
        ImGui::InputTextWithHint("##nv", "value (JSON or text)", &newVal);
        ImGui::SameLine();
        if (ImGui::Button("Add") && !newKey.empty()) {
            try { params[newKey] = json::parse(newVal); } catch (...) { params[newKey] = newVal; }
            newKey.clear(); newVal.clear();
            paramChanged = done = true;
        }
    }
    // behaviour (Lua script)
    if (ImGui::CollapsingHeader("Behaviour (script)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string cur = params.value("script", std::string());
        ImGui::SetNextItemWidth(-90);
        if (ImGui::BeginCombo("script", cur.empty() ? (params.contains("script_src") ? "(inline)" : "(none)") : cur.c_str())) {
            if (ImGui::Selectable("(none)", cur.empty())) { params.erase("script"); paramChanged = done = true; }
            for (const auto& [rel, about] : ed.scripts) {
                if (ImGui::Selectable(rel.c_str(), rel == cur)) { params["script"] = rel; paramChanged = done = true; }
                if (!about.empty() && ImGui::BeginItemTooltip()) { ImGui::TextUnformatted(about.c_str()); ImGui::EndTooltip(); }
            }
            ImGui::EndCombo();
        }
        if (!cur.empty()) {
            if (ImGui::Button("Open in text editor")) OpenURL(fs::absolute(ed.root + "/" + cur).string().c_str());
            ImGui::SameLine();
        }
        static std::string newScript;
        ImGui::SetNextItemWidth(120);
        ImGui::InputTextWithHint("##ns", "new script name", &newScript);
        ImGui::SameLine();
        if (ImGui::Button("Create") && !newScript.empty()) {
            const std::string rel = "scripts/" + newScript + ".lua", full = ed.root + "/" + rel;
            if (!fs::exists(full)) {
                std::ofstream o(full);
                o << "-- " << newScript << ": what this object does.\n"
                  << "-- self: this object (self:param(\"key\", default), self:pos(), self:closed(), self:set_glow(0..1) ...)\n"
                  << "-- world: world.find(name), world.time(), world.air_temp(x,y,z), world.agent_near(x,y,z,r), world.spawn(...)\n"
                  << "-- timers: after(seconds, fn), every(seconds, fn)\n\nfunction on_start(self)\nend\n\nfunction on_tick(self, dt)\nend\n\n"
                  << "function on_press(self)\n  print(self:name() .. \" pressed\")\nend\n\nfunction on_release(self)\nend\n";
            }
            params["script"] = rel;
            paramChanged = done = true;
            ed.RefreshContent();
            OpenURL(fs::absolute(full).string().c_str());
            newScript.clear();
        }
    }
    // write part settings back as overrides
    if (inst && paramChanged) {
        const std::string pre = e->name.substr(inst->name.size() + 1) + ".";
        for (auto p = view.begin(); p != view.end(); ++p) if (p.key() != "_from") target[pre + p.key()] = p.value();
    }
    // wiring and live readings
    if (ed.Electrical(*e) && ImGui::CollapsingHeader("Wiring", ImGuiTreeNodeFlags_DefaultOpen)) {
        int n = 0;
        for (size_t i = 0; i < ed.Z().links.size(); ++i) {
            const TerminalLink l = ed.Z().links[i];
            if (l.entityA != e->id && l.entityB != e->id) continue;
            const bool a = l.entityA == e->id;
            const Entity* o = ed.Z().Find(a ? l.entityB : l.entityA);
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("terminal %d  ->  %s : %d", a ? l.termA : l.termB, o ? o->name.c_str() : "?", a ? l.termB : l.termA);
            ImGui::SameLine();
            if (ImGui::SmallButton("cut")) cutLink = static_cast<int>(i);   // after the panel
            ImGui::PopID();
            ++n;
        }
        if (!n) ImGui::TextDisabled("Not connected. Use the Wire tool (3): drag from a terminal to another.");
        if (const Part* p = ed.world.PartOf(e->id)) {
            ImGui::Text("%.3f V   %.4f A   %.3f W   %s", p->v, p->i, p->power, p->closed ? "closed" : "");
            if (e->type == "battery" && p->capacityAh > 0) ImGui::ProgressBar(static_cast<float>(p->chargeAh / p->capacityAh), {-1, 0}, "charge");
            if (e->type == "lamp") ImGui::ProgressBar(static_cast<float>(p->brightness), {-1, 0}, "brightness");
            if (ed.playing && (e->type == "button" || e->type == "switch" || e->type == "lever" || e->type == "door")) {
                if (ImGui::Button(e->type == "button" ? "Press (hold)" : "Flip")) ed.world.DebugActuate(e->id, e->type == "button" ? true : !p->closed);
                if (e->type == "button" && ImGui::IsItemDeactivated()) ed.world.DebugActuate(e->id, false);
            }
        }
    }
    if (e->type == "plant") {
        World::PlantInfo pi;
        if (ed.world.PlantState(e->id, pi)) ImGui::Text("growing %.0f%%  health %.0f%%  soil %.0f%%  %d fruit%s", pi.growth * 100, pi.health * 100, pi.moisture * 100, pi.fruits, pi.dead ? "  (dead)" : "");
    }
    // (rebuilding replaces the zone's entities: nothing below may use `e`)
    const std::string what = "Edited " + e->name;
    if (cutLink >= 0 && cutLink < static_cast<int>(ed.Z().links.size())) { ed.Z().links.erase(ed.Z().links.begin() + cutLink); done = true; }
    if (live) ed.Rebuild();
    if (done || (paramChanged && !live)) ed.Commit(what);
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------
void DrawContent(Editor& ed) {
    if (!ImGui::Begin("Content", &ed.showContent)) { ImGui::End(); return; }
    if (ImGui::BeginTabBar("content")) {
        std::vector<std::string> cats;
        for (const PaletteItem& p : ed.palette) if (std::find(cats.begin(), cats.end(), p.category) == cats.end()) cats.push_back(p.category);
        for (const std::string& c : cats) {
            if (!ImGui::BeginTabItem(c.c_str())) continue;
            const float w = ImGui::GetContentRegionAvail().x;
            int col = 0;
            const int per = std::max(1, static_cast<int>(w / 118.f));
            for (size_t i = 0; i < ed.palette.size(); ++i) {
                const PaletteItem& p = ed.palette[i];
                if (p.category != c) continue;
                ImGui::PushID(static_cast<int>(i));
                const bool on = ed.placingActive && ed.placing.label == p.label && ed.placing.category == p.category;
                ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImVec4(p.color.r / 400.f, p.color.g / 400.f, p.color.b / 400.f, 1.f));
                if (ImGui::Button(p.label.c_str(), {110, 38})) {
                    ed.placing = p;
                    ed.placingActive = true;
                    ed.tool = Tool::Place;
                    ed.Log("Placing " + p.label + ": click in the view (Shift: keep placing, Esc: stop)");
                }
                ImGui::PopStyleColor();
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("%s  (%s)", p.label.c_str(), p.type.c_str());
                    if (!p.help.empty()) ImGui::TextUnformatted(p.help.c_str());
                    if (!p.plugin.empty()) ImGui::TextDisabled("from plugin %s", p.plugin.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
                if (++col % per) ImGui::SameLine();
            }
            ImGui::NewLine();
            if (c == "Objects") {
                ImGui::SeparatorText("make an object from the selection");
                static std::string objName, objDesc;
                ImGui::SetNextItemWidth(160);
                ImGui::InputTextWithHint("##on", "object name", &objName);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-90);
                ImGui::InputTextWithHint("##od", "what it is / does", &objDesc);
                ImGui::SameLine();
                if (ImGui::Button("Save object") && !objName.empty()) {
                    std::vector<Entity> parts;
                    for (int id : ed.selection) if (const Entity* e = ed.Z().Find(id); e && e->type != "prefab" && !ed.InstanceOf(e)) parts.push_back(*e);
                    if (parts.empty()) ed.Log("Select the parts first (Ctrl+click)");
                    else {
                        Vec3 o{0, 1e9f, 0};
                        for (const Entity& e : parts) { o.x += e.pos.x / parts.size(); o.z += e.pos.z / parts.size(); o.y = std::min(o.y, e.pos.y - 0.5f * e.size.y); }
                        std::string err;
                        if (SavePrefab(ed.root, objName, objDesc.empty() ? "made in the editor" : objDesc, parts, ed.Z().links, o, &err)) {
                            ed.RefreshContent();
                            ed.Log("Saved object " + objName + " (" + std::to_string(parts.size()) + " parts)");
                        } else ed.Log("Save failed: " + err);
                    }
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scripts")) {
            for (const auto& [rel, about] : ed.scripts) {
                ImGui::PushID(rel.c_str());
                ImGui::TextUnformatted(rel.substr(8).c_str());
                ImGui::SameLine(200);
                ImGui::TextDisabled("%s", about.c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
                if (ImGui::SmallButton("give to selection")) {
                    for (int id : ed.selection) if (Entity* e = ed.Z().Find(id)) e->params["script"] = rel;
                    ed.Commit("Script " + rel);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("open")) OpenURL(fs::absolute(ed.root + "/" + rel).string().c_str());
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Maps")) {
            for (const std::string& z : ed.zones)
                if (ImGui::Selectable(z.c_str(), fs::path(ed.path).filename() == z, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) ed.Open(ed.root + "/zones/" + z);
            if (ImGui::Button("New map...")) ed.showNewMap = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void DrawConsole(Editor& ed) {
    if (!ImGui::Begin("Console", &ed.showConsole)) { ImGui::End(); return; }
    if (ImGui::SmallButton("Clear")) ed.console.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("editor messages, then script output");
    ImGui::BeginChild("##log");
    for (const std::string& l : ed.console) ImGui::TextUnformatted(l.c_str());
    for (const std::string& l : ed.world.ScriptLog()) ImGui::TextColored({0.75f, 0.9f, 0.6f, 1.f}, "%s", l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------
// A live circuit diagram laid out like the floor plan (seen from above): parts where they are, wires between them,
// coloured by the current flowing; values update while playing.
void DrawSchematic(Editor& ed) {
    if (!ImGui::Begin("Schematic", &ed.showSchematic)) { ImGui::End(); return; }
    std::vector<const Entity*> parts;
    for (const Entity& e : ed.Z().entities) if (e.part >= 0 && !e.params.value("spawned", false)) parts.push_back(&e);
    if (parts.empty()) { ImGui::TextDisabled("No electrical parts. Place a battery, a lamp and a switch (Content > Electric), then wire them."); ImGui::End(); return; }
    float lx = 1e9f, lz = 1e9f, hx = -1e9f, hz = -1e9f;
    for (const Entity* e : parts) { lx = std::min(lx, e->pos.x); hx = std::max(hx, e->pos.x); lz = std::min(lz, e->pos.z); hz = std::max(hz, e->pos.z); }
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const float pad = 50.f;
    const float sx = std::max(0.1f, hx - lx), sz = std::max(0.1f, hz - lz);
    const float k = std::min((avail.x - 2 * pad) / sx, (avail.y - 2 * pad - 20) / sz);
    // spread parts that sit on top of each other
    std::map<int, ImVec2> at;
    for (const Entity* e : parts) {
        ImVec2 p{o.x + pad + (e->pos.x - lx) * k, o.y + pad + (e->pos.z - lz) * k};
        for (int tries = 0; tries < 20; ++tries) {
            bool clash = false;
            for (auto& [id, q] : at) clash |= std::fabs(q.x - p.x) < 70 && std::fabs(q.y - p.y) < 34;
            if (!clash) break;
            p.y += 36;
        }
        at[e->id] = p;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, {o.x + avail.x, o.y + avail.y}, IM_COL32(22, 24, 28, 255));
    // wires
    for (const TerminalLink& l : ed.Z().links) {
        if (!at.count(l.entityA) || !at.count(l.entityB)) continue;
        const Part* pa = ed.world.PartOf(l.entityA);
        const float amps = pa ? static_cast<float>(std::fabs(pa->i)) : 0.f;
        const float t = std::clamp(amps / 2.f, 0.f, 1.f);
        const ImU32 c = amps > 1e-4f ? IM_COL32(255, static_cast<int>(200 - 120 * t), 60, 255) : IM_COL32(120, 120, 130, 255);
        const ImVec2 a = at[l.entityA], b = at[l.entityB];
        const ImVec2 oa{a.x + (l.termA ? 26.f : -26.f), a.y}, ob{b.x + (l.termB ? 26.f : -26.f), b.y};
        dl->AddBezierCubic(oa, {oa.x + (l.termA ? 40.f : -40.f), oa.y}, {ob.x + (l.termB ? 40.f : -40.f), ob.y}, ob, c, amps > 1e-4f ? 3.f : 1.5f);
    }
    // parts
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const Entity* e : parts) {
        const ImVec2 p = at[e->id];
        const Part* pt = ed.world.PartOf(e->id);
        Rgb body{70, 72, 80};
        if (e->type == "battery") body = {50, 110, 60};
        else if (e->type == "lamp") { const float b = pt ? static_cast<float>(pt->brightness) : 0.f; body = {static_cast<unsigned char>(90 + 160 * b), static_cast<unsigned char>(90 + 150 * b), static_cast<unsigned char>(60 + 40 * b)}; }
        else if (e->type == "switch" || e->type == "button" || e->type == "lever" || e->type == "relay") body = pt && pt->closed ? Rgb{60, 120, 170} : Rgb{90, 70, 70};
        else if (e->type == "heater") body = {150, 80, 50};
        const ImVec2 a{p.x - 26, p.y - 14}, b{p.x + 26, p.y + 14};
        const bool sel = ed.selection.count(e->id) > 0;
        dl->AddRectFilled(a, b, Col(body), 4.f);
        dl->AddRect(a, b, sel ? IM_COL32(255, 220, 60, 255) : IM_COL32(200, 200, 210, 120), 4.f, 0, sel ? 2.f : 1.f);
        dl->AddCircleFilled({a.x, p.y}, 3.5f, IM_COL32(110, 230, 110, 255));
        dl->AddCircleFilled({b.x, p.y}, 3.5f, IM_COL32(110, 230, 110, 255));
        const std::string label = e->type.substr(0, 7);
        dl->AddText({a.x + 4, a.y + 1}, IM_COL32(255, 255, 255, 230), label.c_str());
        if (pt) {
            char v[32];
            std::snprintf(v, sizeof(v), "%.1fV", pt->v);
            dl->AddText({a.x + 4, a.y + 14}, IM_COL32(200, 230, 200, 200), v);
        }
        if (mouse.x > a.x && mouse.x < b.x && mouse.y > a.y && mouse.y < b.y) {
            ImGui::BeginTooltip();
            ImGui::Text("%s (%s)", e->name.c_str(), e->type.c_str());
            if (pt) ImGui::Text("%.3f V  %.4f A  %.3f W%s", pt->v, pt->i, pt->power, pt->closed ? "  closed" : "");
            ImGui::EndTooltip();
            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) { ed.selection = {e->id}; ed.primary = e->id; }
        }
    }
    ImGui::Dummy(avail);
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------
void DrawTerrainPanel(Editor& ed) {
    if (!ImGui::Begin("World", &ed.showTerrain)) { ImGui::End(); return; }
    Zone& z = ed.Z();
    bool done = false, preview = false;
    if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("name", &z.name);
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("air °C", &z.ambientC, 0.1f, -30, 50, "%.1f");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("sun", &z.sun, 0, 1.5f, "%.2f");
        HelpMark("0 = indoors (lamps only). Outdoors ~1.");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        float d[3] = {z.sunDir.x, z.sunDir.y, z.sunDir.z};
        if (ImGui::DragFloat3("sun direction", d, 0.01f, -1, 1)) z.sunDir = Vec3{d[0], d[1], d[2]}.Normalized({0, -1, 0});
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("day length s", &z.dayLength, 10, 10, 86400, "%.0f");
        HelpMark("Real seconds per game day (1440 = a game minute per second, like her body clock).");
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("plant growth x", &z.growth, 0.1f, 0, 500, "%.1f");
        HelpMark("Speeds nature up for testing.");
        done |= ImGui::IsItemDeactivatedAfterEdit();
    }
    Entity* terrain = nullptr;
    for (Entity& e : z.entities) if (e.type == "terrain") { terrain = &e; break; }
    if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!terrain) {
            static float size = 40.f;
            static int seed = 1;
            ImGui::DragFloat("size m", &size, 1, 4, 1000);
            ImGui::DragInt("seed", &seed);
            if (ImGui::Button("Add terrain")) {
                Entity t;
                t.id = z.NextId();
                t.name = "Ground";
                t.type = "terrain";
                t.size = {size, 4.f, size};
                t.color = {86, 128, 58};
                t.params = {{"res", std::clamp(static_cast<int>(size * 3.2f) / 4 * 4, 32, 512)}, {"seed", seed}, {"amp", 1.5}, {"feature", 16}};
                z.entities.insert(z.entities.begin(), t);   // terrain first: other things sit on it
                ed.Commit("Added terrain");
            }
        } else {
            json& q = terrain->params;
            ImGui::Text("%s  %.0f x %.0f m", terrain->name.c_str(), terrain->size.x, terrain->size.z);
            float sz[2] = {terrain->size.x, terrain->size.z};
            if (ImGui::DragFloat2("size m", sz, 0.5f, 2, 2000)) { terrain->size.x = sz[0]; terrain->size.z = sz[1]; }
            const bool sizeDone = ImGui::IsItemDeactivatedAfterEdit();
            bool regen = false;
            for (const ParamDef& d : ed.schemas["terrain"]) regen |= ParamWidget(ed, q, d, done);
            if (sizeDone) { q.erase("heights"); regen = true; done = true; }
            if (q.contains("heights")) {
                ImGui::TextColored({1, 0.8f, 0.4f, 1}, "Sculpted (generation settings are ignored)");
                if (ImGui::Button("Discard sculpting, regenerate")) { q.erase("heights"); done = true; }
            } else if (regen) {
                preview = true;   // live preview of the generator (rebuilt after the panel)
            }
            if (ImGui::Button("New random seed")) { q["seed"] = GetRandomValue(1, 999999); q.erase("heights"); done = true; }
            ImGui::SeparatorText("sculpt (Terrain tool, 4)");
            const char* names[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise", "Pond"};
            for (int i = 0; i < 6; ++i) {
                if (i) ImGui::SameLine();
                const bool on = ed.tool == Tool::Terrain && static_cast<int>(ed.brush) == i;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(names[i])) { ed.brush = static_cast<Brush>(i); ed.tool = Tool::Terrain; }
                if (on) ImGui::PopStyleColor();
            }
            ImGui::SliderFloat("radius m", &ed.brushR, 0.2f, 30.f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("strength", &ed.brushStrength, 0.02f, 3.f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::TextDisabled("Hold LMB in the view. Flatten levels to where you first click. Pond: click to dig a hollow and fill it.");
        }
    }
    if (ImGui::CollapsingHeader("Plants", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* sp[] = {"grass", "flower", "bush", "berry_bush", "tree", "fruit_tree"};
        ImGui::Combo("species", &ed.plantSpecies, sp, 6);
        ImGui::SliderInt("per click", &ed.plantDensity, 1, 60);
        ImGui::SliderFloat("brush radius", &ed.brushR, 0.2f, 30.f, "%.1f m", ImGuiSliderFlags_Logarithmic);
        if (ImGui::Button(ed.tool == Tool::Plants ? "Plant brush: ON" : "Plant brush (5)")) ed.tool = Tool::Plants;
        ImGui::TextDisabled("LMB plants, Shift+LMB clears in the circle.");
        int count[6] = {};
        for (const Entity& e : z.entities)
            if (e.type == "plant") for (int i = 0; i < 6; ++i) if (e.params.value("species", std::string()) == sp[i]) ++count[i];
        ImGui::Text("grass %d  flowers %d  bushes %d  berry %d  trees %d  fruit trees %d", count[0], count[1], count[2], count[3], count[4], count[5]);
    }
    if (done) ed.Commit("Map settings");
    else if (preview) ed.Rebuild();
    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------
void DrawDialogs(Editor& ed) {
    if (ed.showNewMap) { ImGui::OpenPopup("New map"); ed.showNewMap = false; }
    if (ed.showOpen) { ImGui::OpenPopup("Open map"); ed.showOpen = false; }
    if (ed.showSaveAs) { ImGui::OpenPopup("Save map as"); ed.showSaveAs = false; }
    ImGui::SetNextWindowSize({440, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New map", nullptr)) {
        static NewMapSpec s;
        static std::string name = "new_map";
        ImGui::InputText("file name", &name);
        const char* kinds[] = {"outdoor", "room", "empty"};
        int k = s.kind == "room" ? 1 : s.kind == "empty" ? 2 : 0;
        ImGui::Combo("kind", &k, kinds, 3);
        s.kind = kinds[k];
        float size[2] = {s.sizeX, s.sizeZ};
        ImGui::DragFloat2("size m", size, 0.5f, 2, 2000);
        s.sizeX = size[0]; s.sizeZ = size[1];
        if (s.kind == "room") ImGui::DragFloat("wall height", &s.wallH, 0.05f, 1.8f, 10);
        if (s.kind == "outdoor") {
            ImGui::DragInt("seed", &s.seed);
            ImGui::SameLine();
            if (ImGui::SmallButton("random")) s.seed = GetRandomValue(1, 999999);
            ImGui::DragFloat("hills m", &s.hills, 0.05f, 0, 30);
            ImGui::DragFloat("hill size m", &s.feature, 0.5f, 2, 400);
            ImGui::Checkbox("pond", &s.pond);
            ImGui::SameLine();
            ImGui::Checkbox("grass, flowers, trees", &s.plants);
        }
        ImGui::DragFloat("air °C", &s.ambientC, 0.1f, -30, 50);
        if (s.kind == "outdoor") ImGui::SliderFloat("sun", &s.sun, 0, 1.5f);
        const std::string file = ed.root + "/zones/" + name + ".json";
        if (fs::exists(file)) ImGui::TextColored({1, 0.6f, 0.3f, 1}, "%s exists: it will be replaced", (name + ".json").c_str());
        if (ImGui::Button("Create", {120, 0}) && !name.empty()) {
            s.name = name;
            Zone zz = MakeMap(s);
            std::string err;
            if (SaveZone(file, zz, &err)) { ed.Open(file); ed.RefreshContent(); }
            else ed.Log("Couldn't create: " + err);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Open map", nullptr)) {
        for (const std::string& z : ed.zones) if (ImGui::Selectable(z.c_str())) { ed.Open(ed.root + "/zones/" + z); ImGui::CloseCurrentPopup(); }
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Save map as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static std::string name;
        if (name.empty()) name = fs::path(ed.path).stem().string() + "_copy";
        ImGui::InputText("file name", &name);
        if (ImGui::Button("Save", {120, 0}) && !name.empty()) { ed.Save(ed.root + "/zones/" + name + ".json"); name.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) { name.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

} // namespace eon
