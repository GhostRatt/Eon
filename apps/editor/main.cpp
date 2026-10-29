// eon_editor — the map/game-building tool for Eon (Dear ImGui panels around a raylib viewport).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "raylib.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "rlImGui.h"
#include "raymath.h"
#include "rlgl.h"

#include "Editor.h"

using namespace eon;
namespace fs = std::filesystem;

namespace eon {

void Editor::Log(const std::string& s) {
    status = s;
    console.push_back(s);
    if (console.size() > 400) console.erase(console.begin(), console.begin() + 100);
}

void Editor::Rebuild() {
    const Zone z = Z();
    world.SetZone(z);
    world.SetPhysicsEnabled(playing);
}

void Editor::Commit(const std::string& what) {
    const std::string snap = ZoneToJson(Z()).dump();
    if (snap == lastSnapshot) { Rebuild(); return; }
    if (!lastSnapshot.empty()) undo.push_back(lastSnapshot);
    if (undo.size() > 200) undo.erase(undo.begin());
    redo.clear();
    lastSnapshot = snap;
    dirty = true;
    Rebuild();
    if (!what.empty()) status = what;
}

static void LoadSnapshot(Editor& ed, const std::string& snap) {
    Zone z;
    if (!ZoneFromJson(nlohmann::json::parse(snap), z)) return;
    ed.world.SetZone(z);
    ed.world.SetPhysicsEnabled(false);
    ed.lastSnapshot = snap;
    ed.dirty = true;
    for (auto it = ed.selection.begin(); it != ed.selection.end();) it = ed.Z().Find(*it) ? std::next(it) : ed.selection.erase(it);
    if (ed.primary > 0 && !ed.Z().Find(ed.primary)) ed.primary = ed.selection.empty() ? -1 : *ed.selection.begin();
}

void Editor::Undo() {
    if (playing || undo.empty()) return;
    redo.push_back(lastSnapshot);
    const std::string s = undo.back();
    undo.pop_back();
    LoadSnapshot(*this, s);
    Log("Undo");
}

void Editor::Redo() {
    if (playing || redo.empty()) return;
    undo.push_back(lastSnapshot);
    const std::string s = redo.back();
    redo.pop_back();
    LoadSnapshot(*this, s);
    Log("Redo");
}

bool Editor::Open(const std::string& file) {
    std::string err;
    playing = false;
    if (!world.Load(file, &err)) { Log("Open failed: " + err); return false; }
    world.SetPhysicsEnabled(false);
    path = file;
    selection.clear();
    primary = -1;
    undo.clear();
    redo.clear();
    lastSnapshot = ZoneToJson(Z()).dump();
    dirty = false;
    // frame the map: look at it from a corner
    float gx = 0.f;
    Vec3 lo{-3, 0, -3}, hi{3, 2, 3};
    for (const Entity& e : Z().entities)
        if (e.type == "terrain" || e.name == "Floor") { lo = e.pos - e.size * 0.5f; hi = e.pos + e.size * 0.5f; }
    const Vec3 c = (lo + hi) * 0.5f;
    world.GroundHeight(c.x, c.z, gx);
    const float r = std::max(3.f, 0.35f * std::max(hi.x - lo.x, hi.z - lo.z));
    bool indoors = false;
    for (const Entity& e : Z().entities) indoors |= e.name == "Ceiling";
    if (indoors) fly.Init({lo.x + 0.5f, 1.8f, hi.z - 0.5f}, {c.x, 0.7f, c.z});   // inside the room, from a corner
    else fly.Init({c.x - r, gx + 0.6f * r + 1.6f, c.z + r}, {c.x, gx + 0.5f, c.z});
    Log("Opened " + fs::path(file).filename().string());
    return true;
}

bool Editor::Save(const std::string& file) {
    std::string err;
    fs::create_directories(fs::path(file).parent_path());
    if (!SaveZone(file, Z(), &err)) { Log("Save failed: " + err); return false; }
    path = file;
    dirty = false;
    Log("Saved " + fs::path(file).filename().string());
    RefreshContent();
    return true;
}

void Editor::RefreshContent() {
    objects = ListPrefabs(root);
    scripts.clear();
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(root + "/scripts", ec)) {
        if (de.path().extension() != ".lua") continue;
        std::ifstream f(de.path());
        std::string first;
        std::getline(f, first);
        if (first.rfind("--", 0) == 0) first = first.substr(std::min(first.size(), first.find_first_not_of("- ")));
        scripts.push_back({"scripts/" + de.path().filename().string(), first});
    }
    std::sort(scripts.begin(), scripts.end());
    zones.clear();
    for (const auto& de : fs::directory_iterator(root + "/zones", ec))
        if (de.path().extension() == ".json") zones.push_back(de.path().filename().string());
    std::sort(zones.begin(), zones.end());
    BuildPalette(*this);
}

Entity* Editor::InstanceOf(const Entity* e) {
    if (!e || !e->params.contains("_from")) return nullptr;
    const std::string n = e->params["_from"].get<std::string>();
    for (Entity& x : Z().entities) if (x.type == "prefab" && x.name == n) return &x;
    return nullptr;
}

bool Editor::Electrical(const Entity& e) const { return IsElectrical(e.type); }

int Editor::Place(const PaletteItem& item, const Vec3& at) {
    Entity e;
    e.id = Z().NextId();
    e.type = item.type;
    std::string base = item.label;
    std::replace(base.begin(), base.end(), ' ', '_');
    e.name = base + "_" + std::to_string(e.id);
    e.size = item.size;
    e.massKg = item.mass;
    e.color = item.color;
    e.params = item.params;
    e.pos = at;
    e.rotDeg.y = placeYaw;
    if (item.type == "start")   // there's one start: placing it again moves it
        for (Entity& x : Z().entities)
            if (x.type == "start") {
                x.pos = at;
                x.rotDeg.y = placeYaw;
                selection = {x.id};
                primary = x.id;
                Commit("Moved Jenny's start");
                return x.id;
            }
    if (item.type == "prefab" || item.type == "plant" || item.type == "terrain" || item.type == "water") e.pos = at;
    else e.pos.y = at.y + 0.5f * item.size.y + (item.type == "door" ? 0.012f : 0.002f);
    if (snapOn) { e.pos.x = std::round(e.pos.x / snap) * snap; e.pos.z = std::round(e.pos.z / snap) * snap; }
    Z().entities.push_back(e);
    selection = {e.id};
    primary = e.id;
    Commit("Placed " + e.name);
    return e.id;
}

void Editor::DeleteSelection() {
    std::set<int> gone;
    for (int id : selection) {
        const Entity* e = Z().Find(id);
        if (!e) continue;
        if (InstanceOf(e)) continue;   // parts of an object go with the object
        gone.insert(id);
        if (e->type == "prefab") for (const Entity& x : Z().entities) if (x.params.value("_from", std::string()) == e->name) gone.insert(x.id);
    }
    if (gone.empty()) return;
    auto& L = Z().links;
    L.erase(std::remove_if(L.begin(), L.end(), [&](const TerminalLink& l) { return gone.count(l.entityA) || gone.count(l.entityB); }), L.end());
    auto& E = Z().entities;
    E.erase(std::remove_if(E.begin(), E.end(), [&](const Entity& e) { return gone.count(e.id) > 0; }), E.end());
    selection.clear();
    primary = -1;
    Commit("Deleted " + std::to_string(gone.size()));
}

} // namespace eon

static std::string ContentDir() {
    if (DirectoryExists("content/zones")) return "content";
    return EON_CONTENT_DIR;
}

int main(int argc, char** argv) {
    Editor ed;
    ed.root = ContentDir();
    std::string open = ed.root + "/zones/test_zone_01.json", shot, selectName;
    bool camSet = false, doDraw = false;
    Vector3 camPos{}, camTarget{};
    Vector4 drawCmd{};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--select" && i + 1 < argc) selectName = argv[++i];
        else if (a == "--tool" && i + 1 < argc) {   // for screenshots: start in this tool
            const std::string t = argv[++i];
            const std::pair<const char*, Tool> tools[] = {{"move", Tool::Move}, {"wire", Tool::Wire}, {"terrain", Tool::Terrain}, {"plants", Tool::Plants}, {"draw", Tool::Draw},
                                                          {"scale", Tool::Scale}, {"rotate", Tool::Rotate}, {"paint", Tool::Paint}, {"measure", Tool::Measure}};
            for (const auto& [n, tt] : tools) if (t == n) ed.tool = tt;
        }
        else if (a == "--draw" && i + 5 < argc) {   // for screenshots/tests: draw SHAPE from x0 z0 to x1 z1 (on the floor) once loaded
            const std::string sh = argv[i + 1];
            const char* names[] = {"wall", "floor", "box", "room", "stairs", "water", "ramp"};
            for (int k = 0; k < 7; ++k) if (sh == names[k]) ed.shape = static_cast<DrawShape>(k);
            drawCmd = {std::stof(argv[i + 2]), std::stof(argv[i + 3]), std::stof(argv[i + 4]), std::stof(argv[i + 5])};
            doDraw = true;
            i += 5;
        }
        else if (a == "--cam" && i + 6 < argc) {
            camPos = {std::stof(argv[i + 1]), std::stof(argv[i + 2]), std::stof(argv[i + 3])};
            camTarget = {std::stof(argv[i + 4]), std::stof(argv[i + 5]), std::stof(argv[i + 6])};
            camSet = true;
            i += 6;
        } else open = a;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Eon Editor");
    SetExitKey(KEY_NULL);
    SetTargetFPS(120);
    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "eon_editor_layout.ini";
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 4.f;
    st.FrameRounding = 3.f;
    st.GrabRounding = 3.f;

    ed.renderer.Init();
    ed.renderer.ambient = 0.6f;
    ed.world.SetContentRoot(ed.root);
    BuildSchemas(ed);
    ed.RefreshContent();
    LoadPlugins(ed);
    ed.Open(open);
    if (camSet) ed.fly.Init(camPos, camTarget);
    if (doDraw) {
        ed.drawA = {drawCmd.x, 0.f, drawCmd.y};
        ed.drawB = {drawCmd.z, 0.f, drawCmd.w};
        ed.Log("Drew " + std::to_string(FinishDraw(ed)) + " parts");
        ed.Commit("Drew");
    }
    if (!selectName.empty())
        for (const Entity& e : ed.Z().entities) if (e.name == selectName) { ed.selection = {e.id}; ed.primary = e.id; }

    int frameNo = 0;
    bool firstLayout = !fs::exists("eon_editor_layout.ini");
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        WatchPlugins(ed);
        UpdateViewport(ed, dt);
        ed.world.Tick(dt);
        ed.world.BuildRenderFrame(ed.frame);

        // the 3D view, into its texture
        if (ed.viewW > 8 && ed.viewH > 8 && (ed.view.texture.width != ed.viewW || ed.view.texture.height != ed.viewH)) {
            if (ed.view.id) UnloadRenderTexture(ed.view);
            ed.view = LoadRenderTexture(ed.viewW, ed.viewH);
        }
        if (ed.view.id) {
            BeginTextureMode(ed.view);
            ClearBackground(ed.frame.sunColor.x > 0.f ? Color{150, 185, 220, 255} : Color{30, 32, 36, 255});
            const Entity* sel = ed.Sel();
            ed.renderer.Draw(ed.frame, ed.fly.cam, sel && sel->type != "prefab" ? ed.primary : -1, false);
            BeginMode3D(ed.fly.cam);
            DrawViewportOverlay(ed);
            EndMode3D();
            ed.renderer.DrawTranslucent(ed.fly.cam);
            EndTextureMode();
        }

        BeginDrawing();
        ClearBackground({20, 21, 24, 255});
        rlImGuiBegin();
        const ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (firstLayout) {
            // a first, sensible arrangement (afterwards ImGui remembers the user's own in eon_editor_layout.ini)
            firstLayout = false;
            ImGui::DockBuilderRemoveNode(dock);
            ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
            ImGuiID center = dock;
            const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
            const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
            const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
            ImGuiID leftTop = left;
            const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(leftTop, ImGuiDir_Down, 0.45f, nullptr, &leftTop);
            ImGui::DockBuilderDockWindow("Viewport", center);
            ImGui::DockBuilderDockWindow("Outliner", leftTop);
            ImGui::DockBuilderDockWindow("World", leftBottom);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Content", bottom);
            ImGui::DockBuilderDockWindow("Schematic", bottom);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderFinish(dock);
        }
        DrawMenuBar(ed);
        DrawToolbar(ed);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            ed.viewW = std::max(16, static_cast<int>(avail.x));
            ed.viewH = std::max(16, static_cast<int>(avail.y));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ed.viewPos = {p.x, p.y};
            if (ed.view.id) rlImGuiImageRenderTextureFit(&ed.view, false);
            ed.viewHovered = ImGui::IsWindowHovered();
            ed.viewFocused = ImGui::IsWindowFocused();
            // status line over the view
            ImGui::SetCursorScreenPos({p.x + 8, p.y + 6});
            ImGui::TextColored(ed.playing ? ImVec4(0.4f, 1.f, 0.4f, 1.f) : ImVec4(1, 1, 1, 0.85f), "%s%s  %s", ed.playing ? "PLAY  " : "",
                               ed.Z().name.c_str(), ed.status.c_str());
            DrawViewportHud(ed);
        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (ed.showOutliner) DrawOutliner(ed);
        if (ed.showInspector) DrawInspector(ed);
        if (ed.showContent) DrawContent(ed);
        if (ed.showConsole) DrawConsole(ed);
        if (ed.showSchematic) DrawSchematic(ed);
        if (ed.showTerrain) DrawTerrainPanel(ed);
        DrawDialogs(ed);
        if (ed.showDemo) ImGui::ShowDemoWindow(&ed.showDemo);
        rlImGuiEnd();
        EndDrawing();
        if (!shot.empty() && ++frameNo == 12) { TakeScreenshot(shot.c_str()); break; }
    }
    UnloadPlugins(ed);
    if (ed.view.id) UnloadRenderTexture(ed.view);
    ed.renderer.Unload();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
