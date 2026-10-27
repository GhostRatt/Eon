// eon_editor — build zones, make new objects, give them behaviour.
// Edit mode: physics frozen. Palette (left) places parts or saved objects; inspector (right) edits anything,
// including params and the Lua script that makes an object do something. Selections save as new objects.
// Play mode (P): physics + scripts run; grab/push/press to test; P again restores the edited layout.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "raylib.h"
#include "raymath.h"

#include "core/World.h"
#include "render/ZoneRenderer.h"

using namespace eon;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string ContentDir() {
    if (DirectoryExists("content/zones")) return "content";
    return EON_CONTENT_DIR;
}

struct PartDef { const char* type; Vec3 size; float mass; Rgb color; const char* params; };
const PartDef kParts[] = {
    {"static", {0.3f, 0.3f, 0.3f}, 0.f, {150, 150, 155}, "{}"},
    {"dynamic", {0.2f, 0.2f, 0.2f}, 2.f, {170, 130, 90}, "{}"},
    {"battery", {0.06f, 0.15f, 0.1f}, 0.f, {60, 60, 60}, R"({"voltage":12,"internalR":0.05,"capacityAh":7})"},
    {"lamp", {0.06f, 0.1f, 0.06f}, 0.f, {250, 240, 200}, R"({"ratedPower":5,"ratedVoltage":12})"},
    {"button", {0.05f, 0.05f, 0.02f}, 0.f, {240, 240, 240}, R"({"axis":[0,0,1]})"},
    {"switch", {0.02f, 0.12f, 0.08f}, 0.f, {245, 245, 245}, R"({"axis":[1,0,0]})"},
    {"lever", {0.04f, 0.1f, 0.06f}, 0.f, {180, 180, 185}, R"({"axis":[0,0,1],"handle":0.15,"throw":0.5,"detent":0.3,"leverMass":0.15})"},
    {"relay", {0.05f, 0.05f, 0.05f}, 0.f, {230, 230, 230}, R"({"script":"scripts/timer_light.lua"})"},
    {"motor", {0.06f, 0.06f, 0.08f}, 0.f, {120, 120, 130}, R"({"resistance":2,"k":0.01})"},
    {"resistor", {0.03f, 0.01f, 0.01f}, 0.f, {200, 170, 120}, R"({"resistance":100})"},
    {"heater", {0.45f, 0.45f, 0.12f}, 0.f, {150, 110, 90}, R"({"ratedPower":150,"ratedVoltage":12})"},
    {"wire", {0.01f, 0.01f, 0.3f}, 0.f, {180, 60, 40}, R"({"length":0.4})"},
    {"tap", {0.03f, 0.02f, 0.03f}, 0.f, {200, 210, 220}, R"({"valve":"script","rate":45,"speed":0.3})"},
    {"container", {0.075f, 0.1f, 0.075f}, 0.15f, {235, 235, 230}, R"({"capacity":300})"},
    {"bowl", {0.3f, 0.12f, 0.3f}, 0.f, {240, 240, 240}, R"({"capacity":3000})"},
    {"door", {0.8f, 1.95f, 0.04f}, 15.f, {150, 110, 80}, R"({"hingeSide":"left","swing":100,"seal":2,"friction":0.5})"},
    {"food", {0.075f, 0.075f, 0.075f}, 0.15f, {190, 40, 40}, R"({"grams":150,"kcalPerG":0.52,"waterPerG":0.85})"},
};

const char* kScriptTemplate =
    "-- %s: what this object does. Callbacks are optional; delete the ones you don't need.\n"
    "-- self: this object (self:param(\"key\", default), self:pos(), self:closed(), self:set_glow(0..1), ...)\n"
    "-- world: world.find(name), world.time(), world.air_temp(x,y,z), world.agent_near(x,y,z,r), world.spawn(...)\n"
    "-- timers: after(seconds, fn), every(seconds, fn)\n\n"
    "function on_start(self)\nend\n\n"
    "function on_tick(self, dt)\nend\n\n"
    "function on_press(self)\n  print(self:name() .. \" pressed\")\nend\n\n"
    "function on_release(self)\nend\n";

struct Script { std::string rel, about; };

std::vector<Script> ListScripts(const std::string& root) {
    std::vector<Script> out;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(root + "/scripts", ec)) {
        if (de.path().extension() != ".lua") continue;
        std::ifstream f(de.path());
        std::string first;
        std::getline(f, first);
        if (first.rfind("--", 0) == 0) first = first.substr(first.find_first_not_of("- "));
        out.push_back({"scripts/" + de.path().filename().string(), first});
    }
    std::sort(out.begin(), out.end(), [](const Script& a, const Script& b) { return a.rel < b.rel; });
    return out;
}

// One-line text prompt at the bottom of the screen.
struct Prompt {
    bool on = false;
    std::string label, buf;
    std::function<void(const std::string&)> done;
    void Open(const std::string& l, const std::string& init, std::function<void(const std::string&)> fn) { on = true; label = l; buf = init; done = std::move(fn); }
    void Update() {
        if (!on) return;
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed()) if (c >= 32 && c < 127) buf += static_cast<char>(c);
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !buf.empty()) buf.pop_back();
        if (IsKeyPressed(KEY_ESCAPE)) on = false;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { on = false; auto fn = done; fn(buf); }
    }
    void Draw() const {
        if (!on) return;
        const int w = GetScreenWidth(), y = GetScreenHeight() - 56;
        DrawRectangle(0, y, w, 56, Fade(BLACK, 0.85f));
        DrawText(label.c_str(), 16, y + 6, 16, SKYBLUE);
        DrawText((buf + ((static_cast<int>(GetTime() * 2) & 1) ? "_" : "")).c_str(), 16, y + 28, 20, RAYWHITE);
    }
};

json ParseValue(const std::string& s) {
    try { return json::parse(s); } catch (...) { return s; }   // numbers, true/false, [x,y,z], {...}; anything else is text
}

bool ReadVec(const std::string& s, Vec3& v) { return std::sscanf(s.c_str(), "%f %f %f", &v.x, &v.y, &v.z) == 3 || std::sscanf(s.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z) == 3; }

bool Button(int x, int y, int w, int h, const std::string& text, Color c = RAYWHITE, int fs = 16) {
    const Rectangle r{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    if (hot) DrawRectangleRec(r, Fade(SKYBLUE, 0.25f));
    DrawText(text.c_str(), x + 4, y + (h - fs) / 2, fs, c);
    return hot && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

} // namespace

int main(int argc, char** argv) {
    const std::string root = ContentDir();
    std::string path = root + "/zones/test_zone_01.json", shotFile, selectName;
    Vector3 camPos{0, 1.6f, 1.8f}, camTarget{0, 0.8f, 0};
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shotFile = argv[++i];          // render a few frames, save PNG, quit
        else if (a == "--select" && i + 1 < argc) selectName = argv[++i];
        else if (a == "--cam" && i + 6 < argc) { camPos = {std::stof(argv[i + 1]), std::stof(argv[i + 2]), std::stof(argv[i + 3])}; camTarget = {std::stof(argv[i + 4]), std::stof(argv[i + 5]), std::stof(argv[i + 6])}; i += 6; }
        else path = a;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Eon Editor");
    SetExitKey(KEY_NULL);   // Esc cancels things here
    SetTargetFPS(120);

    ZoneRenderer renderer;
    renderer.Init();
    renderer.ambient = 0.6f;

    World world;
    world.SetContentRoot(root);
    std::string status;
    {
        std::string err;
        status = world.Load(path, &err) ? "Opened " + path : "Open failed: " + err;
    }
    world.SetPhysicsEnabled(false);

    FlyCamera fly;
    fly.Init(camPos, camTarget);

    std::vector<PrefabInfo> objects = ListPrefabs(root);
    std::vector<Script> scripts = ListScripts(root);
    std::set<int> picked;            // multi-selection (Shift+click) for "save as object"
    int selected = -1;
    bool dirty = false, editorLight = true, playing = false, showPalette = true, wireMode = false;
    int placing = -1;                // index into kParts, or 1000+object index
    int wireFrom = -1, wireTerm = -1;
    float grabDist = 0.6f, watchAcc = 0.f, paletteScroll = 0.f;
    fs::file_time_type scriptStamp{};
    Prompt prompt;
    RenderFrame frame;
    int frameNo = 0;
    if (!selectName.empty()) for (const Entity& e : world.GetZone().entities) if (e.name == selectName) selected = e.id;

    auto rebuild = [&]() { const Zone z = world.GetZone(); world.SetZone(z); world.SetPhysicsEnabled(playing); dirty = true; };
    auto instanceOf = [&](const Entity* e) -> Entity* {
        if (!e || !e->params.contains("_from")) return nullptr;
        const std::string n = e->params["_from"].get<std::string>();
        for (Entity& x : world.GetZone().entities) if (x.type == "prefab" && x.name == n) return &x;
        return nullptr;
    };
    // Edits a param; on a part inside an object it becomes an override on the instance ("Part.key").
    auto setParam = [&](int id, const std::string& key, const json* value) {
        Entity* e = world.GetZone().Find(id);
        if (!e) return;
        if (Entity* inst = instanceOf(e)) {
            const std::string k = e->name.substr(inst->name.size() + 1) + "." + key;
            if (!inst->params.contains("set")) inst->params["set"] = json::object();
            if (value) inst->params["set"][k] = *value; else inst->params["set"].erase(k);
        } else if (value) e->params[key] = *value;
        else e->params.erase(key);
        rebuild();
    };
    auto boundsOf = [&](int id, Vec3& lo, Vec3& hi) {
        const Entity* e = world.GetZone().Find(id);
        if (!e) return false;
        lo = {1e9f, 1e9f, 1e9f}; hi = {-1e9f, -1e9f, -1e9f};
        auto add = [&](const Entity& x) {
            const float r = 0.5f * std::max(x.size.x, x.size.z);   // yaw-safe
            lo = {std::min(lo.x, x.pos.x - r), std::min(lo.y, x.pos.y - 0.5f * x.size.y), std::min(lo.z, x.pos.z - r)};
            hi = {std::max(hi.x, x.pos.x + r), std::max(hi.y, x.pos.y + 0.5f * x.size.y), std::max(hi.z, x.pos.z + r)};
        };
        if (e->type == "prefab") { for (const Entity& x : world.GetZone().entities) if (x.params.value("_from", std::string()) == e->name) add(x); }
        else add(*e);
        return lo.x < hi.x;
    };
    auto place = [&](int what, const Vec3& at) {
        Zone& z = world.GetZone();
        Entity e;
        e.id = z.NextId();
        if (what >= 1000) {
            const PrefabInfo& o = objects[what - 1000];
            e.type = "prefab";
            e.name = o.name + "_" + std::to_string(e.id);
            e.size = {0.1f, 0.1f, 0.1f};
            e.params = {{"prefab", o.name}};
            e.pos = at;
        } else {
            const PartDef& d = kParts[what];
            e.type = d.type;
            e.name = std::string(d.type) + "_" + std::to_string(e.id);
            e.name[0] = static_cast<char>(std::toupper(e.name[0]));
            e.size = d.size; e.massKg = d.mass; e.color = d.color;
            e.params = json::parse(d.params);
            e.pos = {at.x, at.y + 0.5f * d.size.y + (e.type == "door" ? 0.012f : 0.002f), at.z};   // doors must clear the floor
        }
        z.entities.push_back(e);
        selected = e.id;
        rebuild();
        status = "Placed " + e.name;
    };
    auto scriptPathOf = [&](const Entity* e) -> std::string {
        if (!e) return {};
        const Entity* inst = instanceOf(e);
        std::string rel = e->params.value("script", std::string());
        if (inst) { const std::string k = e->name.substr(inst->name.size() + 1) + ".script"; if (inst->params.contains("set") && inst->params["set"].contains(k)) rel = inst->params["set"][k].get<std::string>(); }
        return rel.empty() ? rel : root + "/" + rel;
    };

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const bool typing = prompt.on;
        prompt.Update();
        const bool ctrl = !typing && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT);
        const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
        auto key = [&](int k) { return !typing && !ctrl && IsKeyPressed(k); };
        if (!typing && !ctrl) fly.Update(dt, world.GrabbedBody() < 0 && !CheckCollisionPointRec(GetMousePosition(), {0, 0, 260, 4000}));

        const int W = GetScreenWidth();
        const Vector2 mouse = GetMousePosition();
        const bool overUi = (showPalette && mouse.x < 260) || (selected > 0 && mouse.x > W - 440 && mouse.y < 520) || typing;

        const Ray ray = GetScreenToWorldRay(mouse, fly.cam);
        RayHit hit;
        const bool hasHit = world.RayCast(ToEon(ray.position), ToEon(ray.direction), 12.f, hit);
        Vec3 aim = hasHit ? hit.point : ToEon(Vector3Add(ray.position, Vector3Scale(ray.direction, 1.5f)));
        if (!hasHit) aim.y = std::max(aim.y, 0.f);

        if (key(KEY_P)) {
            playing = !playing;
            world.EndGrab();
            world.Reset();
            world.SetPhysicsEnabled(playing);
            placing = -1; wireMode = false;
            status = playing ? "PLAY: physics + scripts running (P to stop)" : "EDIT: layout restored";
        }
        if (key(KEY_TAB)) showPalette = !showPalette;
        if (key(KEY_F5)) { world.ReloadScripts(); scripts = ListScripts(root); objects = ListPrefabs(root); status = "Reloaded scripts and objects"; }
        if (key(KEY_ESCAPE)) { placing = -1; wireMode = false; wireFrom = -1; picked.clear(); }

        // hot reload: any script file saved -> scripts restart in place
        if ((watchAcc += dt) > 0.75f) {
            watchAcc = 0.f;
            fs::file_time_type newest{};
            std::error_code ec;
            for (const auto& de : fs::directory_iterator(root + "/scripts", ec)) newest = std::max(newest, fs::last_write_time(de.path(), ec));
            if (scriptStamp != fs::file_time_type{} && newest != scriptStamp) { world.ReloadScripts(); scripts = ListScripts(root); status = "Script changed: reloaded"; }
            scriptStamp = newest;
        }

        Entity* sel = selected > 0 ? world.GetZone().Find(selected) : nullptr;
        bool changed = false;

        if (playing) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hasHit && !overUi) {
                selected = hit.entity;
                world.BeginGrab(hit.body, hit.point);
                grabDist = hit.dist;
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) world.EndGrab();
            if (world.GrabbedBody() >= 0) {
                grabDist = Clamp(grabDist + GetMouseWheelMove() * 0.05f, 0.15f, 2.5f);
                world.SetGrabTarget(ToEon(Vector3Add(ray.position, Vector3Scale(ray.direction, grabDist))));
            }
            if (!typing && IsKeyDown(KEY_F) && hasHit) world.SetPush(hit.body, hit.point, ToEon(Vector3Scale(ray.direction, 4.f)));
            else world.ClearPush();
            if (sel && key(KEY_SPACE)) {
                const Part* p = world.PartOf(selected);
                world.DebugActuate(selected, sel->type == "button" ? true : !(p && p->closed));
            }
            if (sel && sel->type == "button" && IsKeyReleased(KEY_SPACE)) world.DebugActuate(selected, false);
        } else {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUi) {
                if (placing >= 0) {
                    place(placing, aim);
                    if (!shift) placing = -1;   // Shift keeps placing copies
                } else if (wireMode) {
                    if (hasHit) {
                        const auto terms = world.TerminalPositions(hit.entity);
                        int best = -1;
                        float bd = 0.05f;
                        for (size_t t = 0; t < terms.size(); ++t) { const float d = (terms[t] - hit.point).Length(); if (d < bd) { bd = d; best = static_cast<int>(t); } }
                        if (best < 0 && !terms.empty()) best = (terms[0] - hit.point).Length() < (terms.back() - hit.point).Length() ? 0 : static_cast<int>(terms.size()) - 1;
                        if (best < 0) status = "That has no terminals";
                        else if (wireFrom < 0) { wireFrom = hit.entity; wireTerm = best; status = TextFormat("Wire from #%d.%d: click the other terminal", wireFrom, best); }
                        else if (hit.entity != wireFrom) {
                            Zone& z = world.GetZone();
                            auto same = [&](const TerminalLink& l) { return (l.entityA == wireFrom && l.termA == wireTerm && l.entityB == hit.entity && l.termB == best) ||
                                                                             (l.entityB == wireFrom && l.termB == wireTerm && l.entityA == hit.entity && l.termA == best); };
                            const auto it = std::find_if(z.links.begin(), z.links.end(), same);
                            if (it != z.links.end()) { z.links.erase(it); status = "Disconnected"; }
                            else { z.links.push_back({wireFrom, wireTerm, hit.entity, best}); status = TextFormat("Connected #%d.%d - #%d.%d", wireFrom, wireTerm, hit.entity, best); }
                            wireFrom = -1;
                            rebuild();
                        }
                    }
                } else if (hasHit) {
                    const Entity* h = world.GetZone().Find(hit.entity);
                    Entity* inst = instanceOf(h);
                    const int id = (inst && !alt) ? inst->id : hit.entity;   // Alt: a part inside an object
                    if (shift) { if (!picked.insert(id).second) picked.erase(id); }
                    else picked.clear();
                    selected = id;
                } else { selected = -1; picked.clear(); }
            }
            sel = selected > 0 ? world.GetZone().Find(selected) : nullptr;
            const bool lockedPart = sel && instanceOf(sel);

            if (sel && !ctrl && !typing) {
                const float step = shift ? 0.01f : 0.05f;
                Vec3 d{};
                if (IsKeyPressed(KEY_LEFT)) d.x -= step;
                if (IsKeyPressed(KEY_RIGHT)) d.x += step;
                if (IsKeyPressed(KEY_UP)) d.z -= step;
                if (IsKeyPressed(KEY_DOWN)) d.z += step;
                if (IsKeyPressed(KEY_PAGE_UP)) d.y += step;
                if (IsKeyPressed(KEY_PAGE_DOWN)) d.y -= step;
                const bool rot = IsKeyPressed(KEY_R) || IsKeyPressed(KEY_F);
                if ((d.x != 0 || d.y != 0 || d.z != 0 || rot) && lockedPart) status = "Parts move with their object (Ctrl+E breaks it apart)";
                else {
                    if (d.x != 0 || d.y != 0 || d.z != 0) { sel->pos = sel->pos + d; changed = true; }
                    if (IsKeyPressed(KEY_R)) { sel->rotDeg.y = std::fmod(sel->rotDeg.y + 15.f, 360.f); changed = true; }
                    if (IsKeyPressed(KEY_F)) { sel->rotDeg.y = std::fmod(sel->rotDeg.y + 345.f, 360.f); changed = true; }
                }
                if (IsKeyPressed(KEY_DELETE) && !lockedPart) {
                    Zone& z = world.GetZone();
                    std::set<int> gone = {selected};
                    if (sel->type == "prefab") for (const Entity& e : z.entities) if (e.params.value("_from", std::string()) == sel->name) gone.insert(e.id);
                    z.links.erase(std::remove_if(z.links.begin(), z.links.end(), [&](const TerminalLink& l) { return gone.count(l.entityA) || gone.count(l.entityB); }), z.links.end());
                    z.entities.erase(std::remove_if(z.entities.begin(), z.entities.end(), [&](const Entity& e) { return gone.count(e.id); }), z.entities.end());
                    picked.erase(selected);
                    selected = -1;
                    sel = nullptr;
                    changed = true;
                }
            }
            if (ctrl && IsKeyPressed(KEY_D) && sel && !lockedPart) {
                Entity copy = *sel;
                copy.id = world.GetZone().NextId();
                copy.name += "_copy";
                copy.pos.x += 0.1f;
                world.GetZone().entities.push_back(copy);
                selected = copy.id;
                changed = true;
            }
            if (ctrl && IsKeyPressed(KEY_E) && sel && sel->type == "prefab") {
                // break the object apart: its parts become ordinary, editable entities
                const std::string n = sel->name;
                Zone& z = world.GetZone();
                for (Entity& e : z.entities) if (e.params.value("_from", std::string()) == n) e.params.erase("_from");
                const int id = selected;
                z.entities.erase(std::remove_if(z.entities.begin(), z.entities.end(), [id](const Entity& e) { return e.id == id; }), z.entities.end());
                selected = -1;
                changed = true;
                status = "Broke " + n + " into its parts";
            }
            if (ctrl && IsKeyPressed(KEY_G)) {
                std::set<int> ids = picked;
                if (ids.empty() && sel) ids.insert(selected);
                std::vector<Entity> parts;
                for (int id : ids) if (const Entity* e = world.GetZone().Find(id); e && e->type != "prefab" && !instanceOf(e)) parts.push_back(*e);
                if (parts.empty()) status = "Shift+click the parts to make an object from (objects inside can't be nested yet)";
                else prompt.Open(TextFormat("New object from %d parts. Name (letters, _): ", static_cast<int>(parts.size())), "", [&, parts](const std::string& name) {
                    if (name.empty()) return;
                    Vec3 o{0, 1e9f, 0};
                    for (const Entity& e : parts) { o.x += e.pos.x / parts.size(); o.z += e.pos.z / parts.size(); o.y = std::min(o.y, e.pos.y - 0.5f * e.size.y); }
                    std::string err;
                    if (SavePrefab(root, name, "made in the editor", parts, world.GetZone().links, o, &err)) {
                        objects = ListPrefabs(root);
                        status = "Saved objects/" + name + ".json: it's in the palette";
                    } else status = "Save failed: " + err;
                });
            }
            if (key(KEY_K)) { wireMode = !wireMode; wireFrom = -1; placing = -1; status = wireMode ? "WIRE: click a terminal, then another (again = disconnect). K/Esc to stop" : ""; }
            if (key(KEY_N)) placing = 0;
            if (ctrl && IsKeyPressed(KEY_S)) {
                std::string err;
                status = SaveZone(path, world.GetZone(), &err) ? "Saved " + path : "Save failed: " + err;
                dirty = false;
            }
            if (ctrl && IsKeyPressed(KEY_O)) {
                std::string err;
                status = world.Load(path, &err) ? "Reloaded" : "Reload failed: " + err;
                world.SetPhysicsEnabled(false);
                selected = -1;
                dirty = false;
            }
        }

        if (key(KEY_L)) { editorLight = !editorLight; renderer.ambient = editorLight ? 0.6f : 0.1f; }
        if (changed) rebuild();

        world.Tick(dt);
        world.BuildRenderFrame(frame);
        sel = selected > 0 ? world.GetZone().Find(selected) : nullptr;

        BeginDrawing();
        ClearBackground({30, 32, 36, 255});
        renderer.Draw(frame, fly.cam, sel && sel->type != "prefab" ? selected : -1);

        BeginMode3D(fly.cam);
        if (!playing) DrawGrid(20, 0.5f);
        auto box = [&](int id, Color c) { Vec3 lo, hi; if (boundsOf(id, lo, hi)) DrawCubeWiresV(ToRl((lo + hi) * 0.5f), ToRl(hi - lo), c); };
        if (sel && sel->type == "prefab") box(selected, YELLOW);
        for (int id : picked) box(id, ORANGE);
        if (sel) for (const Vec3& t : world.TerminalPositions(selected)) DrawSphere(ToRl(t), 0.004f, GREEN);
        if (wireMode) {
            for (const TerminalLink& l : world.GetZone().links) {
                const auto a = world.TerminalPositions(l.entityA), b = world.TerminalPositions(l.entityB);
                if (l.termA < static_cast<int>(a.size()) && l.termB < static_cast<int>(b.size())) DrawLine3D(ToRl(a[l.termA]), ToRl(b[l.termB]), YELLOW);
            }
            if (hasHit) for (const Vec3& t : world.TerminalPositions(hit.entity)) DrawSphere(ToRl(t), 0.006f, LIME);
            if (wireFrom > 0) { const auto a = world.TerminalPositions(wireFrom); if (wireTerm < static_cast<int>(a.size())) DrawLine3D(ToRl(a[wireTerm]), ToRl(aim), ORANGE); }
        }
        if (placing >= 0 && !overUi) {
            Vec3 s{0.2f, 0.2f, 0.2f};
            if (placing < 1000) s = kParts[placing].size;
            DrawCubeWiresV(ToRl(aim + Vec3{0, 0.5f * s.y, 0}), ToRl(s), SKYBLUE);
        }
        if (world.GrabbedBody() >= 0) DrawSphere(ToRl(world.GrabPoint()), 0.006f, ORANGE);
        EndMode3D();

        // ---- top bar ----
        const int px = showPalette ? 268 : 8;
        DrawRectangle(px, 8, 720, 112, Fade(BLACK, 0.6f));
        DrawText(TextFormat("%s%s   [%s]%s", world.GetZone().name.c_str(), dirty ? "  *unsaved*" : "", playing ? "PLAY" : "EDIT", wireMode ? "  [WIRE]" : ""),
                 px + 8, 14, 20, playing ? GREEN : RAYWHITE);
        DrawText("LMB select (Shift add, Alt part inside object)  Arrows/PgUp/PgDn move  R/F rotate  Del  Ctrl+D dup", px + 8, 40, 14, LIGHTGRAY);
        DrawText("Tab palette  K wire  Ctrl+G save selection as object  Ctrl+E break object apart  F5 reload  Ctrl+S save", px + 8, 58, 14, LIGHTGRAY);
        DrawText("P play/stop (LMB grab, F push, Space press/flip)  L light  RMB look  WASD/QE fly", px + 8, 76, 14, LIGHTGRAY);
        DrawText(status.c_str(), px + 8, 98, 16, SKYBLUE);

        // ---- palette ----
        if (showPalette) {
            const int H = GetScreenHeight();
            DrawRectangle(0, 0, 260, H, Fade(BLACK, 0.72f));
            if (mouse.x < 260) paletteScroll = std::max(0.f, paletteScroll - GetMouseWheelMove() * 40.f);
            int y = 10 - static_cast<int>(paletteScroll);
            DrawText("PARTS", 10, y, 18, GOLD); y += 26;
            for (int i = 0; i < static_cast<int>(sizeof(kParts) / sizeof(kParts[0])); ++i, y += 22)
                if (Button(8, y, 244, 22, kParts[i].type, placing == i ? SKYBLUE : RAYWHITE) && !playing) { placing = i; wireMode = false; status = "Click to place (Shift: keep placing, Esc: stop)"; }
            y += 10;
            DrawText("OBJECTS", 10, y, 18, GOLD); y += 26;
            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                if (Button(8, y, 244, 22, objects[i].name, placing == 1000 + i ? SKYBLUE : RAYWHITE) && !playing) { placing = 1000 + i; wireMode = false; status = objects[i].description; }
                y += 22;
            }
            y += 10;
            DrawText("BEHAVIOURS (scripts)", 10, y, 18, GOLD); y += 24;
            for (const Script& s : scripts) {
                const bool give = Button(8, y, 244, 20, s.rel.substr(8), sel && !playing ? RAYWHITE : GRAY, 15);
                if (give && sel && !playing) { const json v = s.rel; setParam(selected, "script", &v); status = "Gave " + std::string(sel->name) + " " + s.rel; }
                DrawText(s.about.substr(0, 38).c_str(), 14, y + 20, 10, GRAY);
                y += 34;
            }
            if (Button(8, y, 244, 22, "+ new script for selection", sel && !playing ? LIME : GRAY) && sel && !playing) {
                const int id = selected;
                prompt.Open("New script name (saved to content/scripts/NAME.lua, then opened):", sel->name, [&, id](const std::string& n) {
                    if (n.empty()) return;
                    const std::string rel = "scripts/" + n + ".lua", full = root + "/" + rel;
                    if (!fs::exists(full)) { std::ofstream f(full); f << TextFormat(kScriptTemplate, n.c_str()); }
                    const json v = rel;
                    setParam(id, "script", &v);
                    scripts = ListScripts(root);
                    OpenURL(fs::absolute(full).string().c_str());
                    status = "Wrote " + rel + ": edit and save it; it reloads by itself";
                });
            }
        }

        // ---- inspector ----
        if (sel) {
            const int x = W - 432;
            DrawRectangle(x - 8, 8, 432, 512, Fade(BLACK, 0.7f));
            const Entity* inst = instanceOf(sel);
            DrawText(TextFormat("#%d %s", sel->id, sel->name.c_str()), x, 14, 20, YELLOW);
            if (inst) DrawText(TextFormat("part of %s: edits become overrides", inst->name.c_str()), x, 36, 14, ORANGE);
            int y = 56;
            const int id = selected;
            auto row = [&](const std::string& label, const std::string& value, std::function<void(const std::string&)> fn, bool editable = true) {
                const bool click = Button(x, y, 416, 20, label + "  " + value, editable && !playing ? RAYWHITE : GRAY, 15);
                if (click && editable && !playing) prompt.Open(label + ":", value, std::move(fn));
                y += 21;
            };
            auto vec = [](const Vec3& v) { return std::string(TextFormat("%.3f %.3f %.3f", v.x, v.y, v.z)); };
            auto withSel = [&, id](std::function<void(Entity&)> f) { if (Entity* e = world.GetZone().Find(id)) { f(*e); rebuild(); } };
            const bool own = !inst;
            row("name", sel->name, [&, id](const std::string& s) { withSel([&](Entity& e) { if (!s.empty()) e.name = s; }); }, own && sel->type != "prefab");
            row("type", sel->type, [&, id](const std::string& s) { withSel([&](Entity& e) { if (!s.empty()) e.type = s; }); }, own && sel->type != "prefab");
            row("pos", vec(sel->pos), [&, id](const std::string& s) { withSel([&](Entity& e) { ReadVec(s, e.pos); }); }, own);
            row("rot", vec(sel->rotDeg), [&, id](const std::string& s) { withSel([&](Entity& e) { ReadVec(s, e.rotDeg); }); }, own);
            if (sel->type != "prefab") {
                row("size", vec(sel->size), [&, id](const std::string& s) { withSel([&](Entity& e) { ReadVec(s, e.size); }); }, own);
                row("mass kg", TextFormat("%.3f", sel->massKg), [&, id](const std::string& s) { withSel([&](Entity& e) { e.massKg = std::max(0.f, static_cast<float>(std::atof(s.c_str()))); }); }, own);
                row("color", TextFormat("%d %d %d", sel->color.r, sel->color.g, sel->color.b), [&, id](const std::string& s) {
                    withSel([&](Entity& e) { int r, g, b; if (std::sscanf(s.c_str(), "%d %d %d", &r, &g, &b) == 3) e.color = {static_cast<unsigned char>(r), static_cast<unsigned char>(g), static_cast<unsigned char>(b)}; }); }, own);
            }
            y += 6;
            DrawText("PARAMS  (click to edit; JSON or text; empty = remove)", x, y, 14, GOLD); y += 20;
            for (auto it = sel->params.begin(); it != sel->params.end() && y < 470; ++it) {
                const std::string k = it.key();
                if (k == "_from") continue;
                std::string v = it->dump();
                if (v.size() > 44) v = v.substr(0, 41) + "...";
                row(k, v, [&, id, k](const std::string& s) { if (s.empty()) setParam(id, k, nullptr); else { const json j = ParseValue(s); setParam(id, k, &j); } });
            }
            row("+ add param", "(key=value)", [&, id](const std::string& s) {
                const size_t eq = s.find('=');
                if (eq == std::string::npos || eq == 0) return;
                const json j = ParseValue(s.substr(eq + 1));
                setParam(id, s.substr(0, eq), &j);
            });
            const std::string sp = scriptPathOf(sel);
            if (!sp.empty() && Button(x, y, 200, 20, "open script", LIME, 15)) OpenURL(fs::absolute(sp).string().c_str());
            if (const Part* p = world.PartOf(selected)) {
                y = 474;
                DrawText(TextFormat("%.3f V   %.4f A   %.3f W   %s", p->v, p->i, p->power, p->closed ? "closed" : ""), x, y, 15, GREEN);
                if (sel->type == "battery" && p->capacityAh > 0) DrawText(TextFormat("charge %.1f%%", 100.0 * p->chargeAh / p->capacityAh), x, y + 18, 15, GREEN);
                if (sel->type == "lamp") DrawText(TextFormat("brightness %.0f%%", 100.0 * p->brightness), x, y + 18, 15, GREEN);
            }
        }

        // ---- script log ----
        {
            const auto& log = world.ScriptLog();
            const int n = std::min<int>(6, static_cast<int>(log.size()));
            if (n > 0) {
                const int x = showPalette ? 268 : 8, y0 = GetScreenHeight() - (prompt.on ? 64 : 8) - n * 18 - 8;
                DrawRectangle(x, y0, 720, n * 18 + 8, Fade(BLACK, 0.6f));
                for (int k = 0; k < n; ++k) DrawText(log[log.size() - n + k].c_str(), x + 8, y0 + 4 + k * 18, 14, Color{200, 220, 160, 255});
            }
        }
        prompt.Draw();
        EndDrawing();
        if (!shotFile.empty() && ++frameNo == 8) { TakeScreenshot(shotFile.c_str()); break; }
    }

    renderer.Unload();
    CloseWindow();
    return 0;
}
