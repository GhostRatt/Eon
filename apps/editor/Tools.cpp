// Viewport input: camera, selection, move gizmo, placing, wiring, terrain and plant brushes; and the 3D overlay.
#include <algorithm>
#include <cmath>

#include "raylib.h"
#include "imgui.h"
#include "raymath.h"
#include "rlgl.h"

#include "Editor.h"

namespace eon {

namespace {
Vector2 Screen(Editor& ed, const Vec3& p) {
    const Vector2 s = GetWorldToScreenEx(ToRl(p), ed.fly.cam, ed.viewW, ed.viewH);
    return {s.x + ed.viewPos.x, s.y + ed.viewPos.y};
}
float SegDist2D(Vector2 p, Vector2 a, Vector2 b) {
    const Vector2 ab = Vector2Subtract(b, a);
    const float t = std::clamp(Vector2DotProduct(Vector2Subtract(p, a), ab) / std::max(1e-6f, Vector2DotProduct(ab, ab)), 0.f, 1.f);
    return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t)));
}
// closest point on the line (o + t*d) to the ray
float AxisParam(const Ray& r, const Vec3& o, const Vec3& d) {
    const Vec3 ro = ToEon(r.position), rd = ToEon(r.direction);
    const Vec3 w = o - ro;
    const float a = d.Dot(d), b = d.Dot(rd), c = rd.Dot(rd), dd = d.Dot(w), e = rd.Dot(w);
    const float den = a * c - b * b;
    return std::fabs(den) < 1e-6f ? 0.f : (b * e - c * dd) / den;
}
Vec3 SelectionCenter(Editor& ed) {
    Vec3 c{};
    int n = 0;
    for (int id : ed.selection) if (const Entity* e = ed.Z().Find(id)) { c = c + e->pos; ++n; }
    return n ? c * (1.f / n) : Vec3{};
}
const char* kSpecies[] = {"grass", "flower", "bush", "berry_bush", "tree", "fruit_tree"};

// where the mouse ray meets the level plane at height y
Vec3 PlanePoint(const Ray& r, float y, const Vec3& fallback) {
    if (std::fabs(r.direction.y) < 1e-4f) return fallback;
    const float t = (y - r.position.y) / r.direction.y;
    if (t <= 0.f || t > 500.f) return fallback;
    return ToEon(Vector3Add(r.position, Vector3Scale(r.direction, t)));
}
float SnapV(const Editor& ed, float v, bool shift) { return ed.snapOn && !shift ? std::round(v / ed.snap) * ed.snap : v; }

Entity MakeBox(Zone& z, const std::string& base, const char* type, const Vec3& pos, const Vec3& size, Rgb color, float yawDeg = 0.f, float mass = 0.f) {
    Entity e;
    e.id = z.NextId();
    e.name = base + "_" + std::to_string(e.id);
    e.type = type;
    e.pos = pos;
    e.size = size;
    e.color = color;
    e.rotDeg = {0, yawDeg, 0};
    e.massKg = mass;
    return e;
}

// the six face handles of a box (local axis, sign) in world space
Vec3 FaceDir(const Entity& e, int f) {
    const Vec3 l[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    return Quat::FromEulerDeg(e.rotDeg).Rotate(l[f]);
}
float FaceHalf(const Entity& e, int f) { return 0.5f * (f < 2 ? e.size.x : f < 4 ? e.size.y : e.size.z); }
bool Scalable(Editor& ed, const Entity* e) { return e && !ed.InstanceOf(e) && e->type != "prefab" && e->type != "plant" && e->type != "terrain"; }

}   // namespace

// Builds what the draw tool dragged out (ed.drawA -> ed.drawB). Returns how many things it made.
int FinishDraw(Editor& ed) {
    Zone& z = ed.Z();
    const Vec3 a = ed.drawA, b = ed.drawB;
    const float dx = b.x - a.x, dz = b.z - a.z, len = std::hypot(dx, dz);
    const float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), z0 = std::min(a.z, b.z), z1 = std::max(a.z, b.z);
    const float w = x1 - x0, d = z1 - z0, H = ed.drawH, T = ed.drawT, y = a.y;
    const Vec3 mid{(x0 + x1) * 0.5f, y, (z0 + z1) * 0.5f};
    std::vector<Entity> out;
    switch (ed.shape) {
    case DrawShape::Wall: {
        if (len < 0.05f) return 0;
        const float yaw = std::atan2(-dz, dx) * RAD2DEG;
        out.push_back(MakeBox(z, "Wall", "static", {(a.x + b.x) * 0.5f, y + H * 0.5f, (a.z + b.z) * 0.5f}, {len + T, H, T}, ed.drawColor, yaw));
        break;
    }
    case DrawShape::Floor:
        if (w < 0.05f || d < 0.05f) return 0;
        out.push_back(MakeBox(z, "Floor", "static", {mid.x, y + T * 0.5f, mid.z}, {w, T, d}, ed.drawColor));
        break;
    case DrawShape::Box: {
        if (w < 0.02f || d < 0.02f) return 0;
        const float mass = ed.drawDynamic ? std::clamp(w * d * H * 400.f, 0.05f, 400.f) : 0.f;   // wood-ish
        out.push_back(MakeBox(z, ed.drawDynamic ? "Crate" : "Block", ed.drawDynamic ? "dynamic" : "static", {mid.x, y + H * 0.5f + 0.001f, mid.z}, {w, H, d}, ed.drawColor, 0.f, mass));
        break;
    }
    case DrawShape::Water: {
        if (w < 0.1f || d < 0.1f) return 0;
        Entity e = MakeBox(z, "Water", "water", {mid.x, y - H * 0.5f, mid.z}, {w, H, d}, {70, 110, 140});
        e.params = {{"tempC", 16}};
        out.push_back(e);
        break;
    }
    case DrawShape::Ramp: {
        if (len < 0.1f) return 0;
        // a slab tilted from a (low) up to b, rising drawH over the run
        const float rise = std::min(H, len), slope = std::atan2(rise, len);
        const float yaw = std::atan2(dx, dz) * RAD2DEG;
        const float L = std::hypot(len, rise);
        Entity e = MakeBox(z, "Ramp", "static", {(a.x + b.x) * 0.5f, y + rise * 0.5f, (a.z + b.z) * 0.5f}, {ed.stairW, T, L}, ed.drawColor, yaw);
        e.rotDeg.x = -slope * RAD2DEG;
        out.push_back(e);
        break;
    }
    case DrawShape::Stairs: {
        if (len < 0.2f) return 0;
        const int n = std::max(1, static_cast<int>(std::lround(H / 0.17f)));
        const float rise = H / n, run = len / n;
        const Vec3 dir{dx / len, 0, dz / len};
        const float yaw = std::atan2(dx, dz) * RAD2DEG;
        for (int i = 0; i < n; ++i) {
            const Vec3 c{a.x + dir.x * run * (i + 0.5f), y + rise * (i + 1) * 0.5f, a.z + dir.z * run * (i + 0.5f)};
            Entity e = MakeBox(z, "Step", "static", c, {ed.stairW, rise * (i + 1), run}, ed.drawColor, yaw);
            z.entities.push_back(e);   // ids must advance
            out.push_back(e);
        }
        z.entities.resize(z.entities.size() - out.size());
        break;
    }
    case DrawShape::Room: {
        if (w < 1.f || d < 1.f) return 0;
        auto add = [&](const std::string& n, const Vec3& p, const Vec3& s) { Entity e = MakeBox(z, n, "static", p, s, ed.drawColor); z.entities.push_back(e); out.push_back(e); };
        const float fy = ed.roomFloor ? y + T : y;
        if (ed.roomFloor) add("Floor", {mid.x, y + T * 0.5f, mid.z}, {w, T, d});
        add("Wall", {mid.x, fy + H * 0.5f, z1 - T * 0.5f}, {w, H, T});
        add("Wall", {x0 + T * 0.5f, fy + H * 0.5f, mid.z}, {T, H, d - 2 * T});
        add("Wall", {x1 - T * 0.5f, fy + H * 0.5f, mid.z}, {T, H, d - 2 * T});
        const float zs = z0 + T * 0.5f;
        if (ed.roomDoor && w > 1.6f) {
            // a doorway in the near (-z) wall with a door that opens outward; the bolt is inside
            const float gap = 1.04f, seg = (w - gap) * 0.5f;
            add("Wall", {x0 + seg * 0.5f, fy + H * 0.5f, zs}, {seg, H, T});
            add("Wall", {x1 - seg * 0.5f, fy + H * 0.5f, zs}, {seg, H, T});
            if (H > 2.13f) add("Wall", {mid.x, fy + 2.12f + (H - 2.12f) * 0.5f, zs}, {gap, H - 2.12f, T});
            Entity door = MakeBox(z, "Door", "prefab", {mid.x, fy, zs}, {0.1f, 0.1f, 0.1f}, {150, 110, 80}, 180.f);
            door.params = {{"prefab", "room_door"}};
            z.entities.push_back(door);
            out.push_back(door);
        } else add("Wall", {mid.x, fy + H * 0.5f, zs}, {w, H, T});
        if (ed.roomCeiling) add("Ceiling", {mid.x, fy + H + T * 0.5f, mid.z}, {w, T, d});
        z.entities.resize(z.entities.size() - out.size());
        break;
    }
    }
    ed.selection.clear();
    for (Entity& e : out) { z.entities.push_back(e); ed.selection.insert(e.id); }
    ed.primary = out.empty() ? -1 : out.front().id;
    return static_cast<int>(out.size());
}

void UpdateViewport(Editor& ed, float dt) {
    ImGuiIO& io = ImGui::GetIO();
    const bool typing = io.WantTextInput;
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    // shortcuts
    if (!typing) {
        if (ctrl && IsKeyPressed(KEY_Z)) ed.Undo();
        if (ctrl && IsKeyPressed(KEY_Y)) ed.Redo();
        if (ctrl && IsKeyPressed(KEY_S)) ed.Save(ed.path);
        if (ctrl && IsKeyPressed(KEY_D)) ed.Duplicate();
        if (ctrl && IsKeyPressed(KEY_C)) ed.Copy();
        if (ctrl && IsKeyPressed(KEY_V) && !ed.playing) ed.Paste(ed.aim);
        if (ctrl && IsKeyPressed(KEY_N)) ed.showNewMap = true;
        if (ctrl && IsKeyPressed(KEY_O)) { ed.RefreshContent(); ed.showOpen = true; }
        if (ctrl && IsKeyPressed(KEY_A)) { ed.selection.clear(); for (const Entity& e : ed.Z().entities) if (!e.params.contains("_from") && e.type != "plant") ed.selection.insert(e.id); }
        if (IsKeyPressed(KEY_DELETE) && !ed.playing) ed.DeleteSelection();
        if (IsKeyPressed(KEY_ESCAPE)) { ed.tool = Tool::Select; ed.placingActive = false; ed.wireFromEntity = 0; ed.drawing = false; ed.measureStage = 0; }
        if (IsKeyPressed(KEY_END) && !ed.playing) ed.DropToFloor();
        if (IsKeyPressed(KEY_F5)) ed.PlayWithJenny();
        if (!ctrl && !ed.looking) {
            if (IsKeyPressed(KEY_ONE)) ed.tool = Tool::Select;
            if (IsKeyPressed(KEY_TWO)) ed.tool = Tool::Move;
            if (IsKeyPressed(KEY_THREE)) ed.tool = Tool::Wire;
            if (IsKeyPressed(KEY_FOUR)) ed.tool = Tool::Terrain;
            if (IsKeyPressed(KEY_FIVE)) ed.tool = Tool::Plants;
            if (IsKeyPressed(KEY_SIX)) ed.tool = Tool::Draw;
            if (IsKeyPressed(KEY_SEVEN)) ed.tool = Tool::Scale;
            if (IsKeyPressed(KEY_EIGHT)) ed.tool = Tool::Rotate;
            if (IsKeyPressed(KEY_NINE)) ed.tool = Tool::Paint;
            if (IsKeyPressed(KEY_ZERO)) ed.tool = Tool::Measure;
            if (IsKeyPressed(KEY_F) && !ed.playing) ed.FrameSelection();
            if (IsKeyPressed(KEY_P)) { ed.playing = !ed.playing; ed.world.EndGrab(); ed.Rebuild(); ed.Log(ed.playing ? "Playing" : "Stopped: layout restored"); }
        }
    }
    // camera: fly while the right mouse is held (started over the view)
    static Vector2 rmbAt{};
    static double rmbTime = 0.0;
    if (ed.viewHovered && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { ed.looking = true; rmbAt = GetMousePosition(); rmbTime = GetTime(); }
    const bool rmbClick = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && ed.looking && Vector2Distance(rmbAt, GetMousePosition()) < 4.f && GetTime() - rmbTime < 0.35;
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) ed.looking = false;
    if ((ed.viewHovered || ed.looking) && !typing && !ctrl) ed.fly.Update(dt, ed.viewHovered && !(ed.tool == Tool::Terrain || ed.tool == Tool::Plants));

    // what's under the mouse
    ed.hasHit = false;
    if (ed.viewW > 0 && ed.viewH > 0) {
        const Vector2 m = GetMousePosition();
        ed.mouseRay = GetScreenToWorldRayEx({m.x - ed.viewPos.x, m.y - ed.viewPos.y}, ed.fly.cam, ed.viewW, ed.viewH);
        ed.hasHit = ed.world.RayCast(ToEon(ed.mouseRay.position), ToEon(ed.mouseRay.direction), 400.f, ed.hit);
        if (ed.hasHit) ed.aim = ed.hit.point;
        else {
            // the y=0 plane, or 3 m out
            const float t = std::fabs(ed.mouseRay.direction.y) > 1e-3f ? -ed.mouseRay.position.y / ed.mouseRay.direction.y : -1.f;
            ed.aim = ToEon(Vector3Add(ed.mouseRay.position, Vector3Scale(ed.mouseRay.direction, t > 0.f && t < 300.f ? t : 3.f)));
        }
    }
    if (!ed.viewHovered && !ed.stroking && ed.gizmoAxis < 0 && ed.wireFromEntity == 0 && ed.scaleFace < 0 && !ed.rotating && !ed.drawing) return;
    const bool lmbDown = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ed.viewHovered;
    const Vector2 mouse = GetMousePosition();

    if (ed.playing) {
        static float grabDist = 0.6f;
        if (lmbDown && ed.hasHit) { ed.selection = {ed.hit.entity}; ed.primary = ed.hit.entity; ed.world.BeginGrab(ed.hit.body, ed.hit.point); grabDist = ed.hit.dist; }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) ed.world.EndGrab();
        if (ed.world.GrabbedBody() >= 0) {
            grabDist = std::clamp(grabDist + GetMouseWheelMove() * 0.05f, 0.15f, 4.f);
            ed.world.SetGrabTarget(ToEon(Vector3Add(ed.mouseRay.position, Vector3Scale(ed.mouseRay.direction, grabDist))));
        }
        if (!typing && IsKeyDown(KEY_F) && ed.hasHit) ed.world.SetPush(ed.hit.body, ed.hit.point, ToEon(Vector3Scale(ed.mouseRay.direction, 4.f)));
        else ed.world.ClearPush();
        if (const Entity* s = ed.Sel(); s && !typing && IsKeyPressed(KEY_SPACE)) {
            const Part* p = ed.world.PartOf(s->id);
            ed.world.DebugActuate(s->id, s->type == "button" ? true : !(p && p->closed));
        }
        if (const Entity* s = ed.Sel(); s && s->type == "button" && IsKeyReleased(KEY_SPACE)) ed.world.DebugActuate(s->id, false);
        return;
    }

    auto pick = [&]() {
        // Jenny's start has no body: pick it by its marker on screen
        for (const Entity& e : ed.Z().entities) {
            if (e.type != "start") continue;
            const Vector2 a = Screen(ed, e.pos + Vec3{0, 0.1f, 0}), b = Screen(ed, e.pos + Vec3{0, 1.6f, 0});
            const float camD = Distance(ToEon(ed.fly.cam.position), e.pos);
            if (SegDist2D(mouse, a, b) < 14.f && (!ed.hasHit || ed.hit.dist > camD - 0.3f)) {
                if (ctrl) { if (!ed.selection.insert(e.id).second) ed.selection.erase(e.id); }
                else ed.selection = {e.id};
                ed.primary = e.id;
                return;
            }
        }
        if (!ed.hasHit) { if (!ctrl) { ed.selection.clear(); ed.primary = -1; } return; }
        const Entity* h = ed.Z().Find(ed.hit.entity);
        Entity* inst = ed.InstanceOf(h);
        const int id = inst && !IsKeyDown(KEY_LEFT_ALT) ? inst->id : ed.hit.entity;
        if (ctrl) { if (!ed.selection.insert(id).second) ed.selection.erase(id); }
        else ed.selection = {id};
        ed.primary = id;
    };

    switch (ed.tool) {
    case Tool::Select:
        if (lmbDown) pick();
        break;
    case Tool::Place:
        if (!typing && IsKeyPressed(KEY_R)) ed.placeYaw = std::fmod(ed.placeYaw + (shift ? -ed.rotSnap : ed.rotSnap) + 360.f, 360.f);
        if (lmbDown && ed.placingActive) {
            Vec3 at = ed.aim;
            ed.Place(ed.placing, at);
            if (!shift) { ed.placingActive = false; ed.tool = Tool::Move; }
        }
        break;
    case Tool::Move: {
        const Vec3 c = SelectionCenter(ed);
        const float len = std::max(0.3f, Distance(ToEon(ed.fly.cam.position), c) * 0.15f);
        const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        if (lmbDown) {
            ed.gizmoAxis = -1;
            if (!ed.selection.empty())
                for (int a = 0; a < 3; ++a)
                    if (SegDist2D(mouse, Screen(ed, c), Screen(ed, c + axes[a] * len)) < 9.f) ed.gizmoAxis = a;
            if (ed.gizmoAxis >= 0) {
                ed.gizmoStart = c;
                ed.gizmoGrab = axes[ed.gizmoAxis] * AxisParam(ed.mouseRay, c, axes[ed.gizmoAxis]);
                ed.dragStartPos.clear();
                for (int id : ed.selection) if (const Entity* e = ed.Z().Find(id); e && !ed.InstanceOf(e)) ed.dragStartPos[id] = e->pos;
            } else pick();
        }
        if (ed.gizmoAxis >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vec3 ax = axes[ed.gizmoAxis];
            float t = AxisParam(ed.mouseRay, ed.gizmoStart, ax) - ed.gizmoGrab.Dot(ax);
            if (ed.snapOn && !shift) t = std::round(t / ed.snap) * ed.snap;
            for (auto& [id, p0] : ed.dragStartPos) if (Entity* e = ed.Z().Find(id)) e->pos = p0 + ax * t;
            ed.Rebuild();
        }
        if (ed.gizmoAxis >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ed.gizmoAxis = -1; ed.Commit("Moved"); }
        // turn the selection 15° with R (Shift+R the other way)
        if (!typing && !ed.selection.empty() && IsKeyPressed(KEY_R) && !ed.looking) {
            for (int id : ed.selection) if (Entity* e = ed.Z().Find(id); e && !ed.InstanceOf(e)) e->rotDeg.y = std::fmod(e->rotDeg.y + (shift ? -ed.rotSnap : ed.rotSnap) + 360.f, 360.f);
            ed.Commit("Rotated");
        }
        break;
    }
    case Tool::Wire: {
        // nearest terminal to the mouse (screen space)
        int bestE = 0, bestT = -1;
        float bestD = 12.f;
        for (const Entity& e : ed.Z().entities) {
            if (e.part < 0) continue;
            const auto terms = ed.world.TerminalPositions(e.id);
            for (size_t t = 0; t < terms.size(); ++t) {
                const float d = Vector2Distance(mouse, Screen(ed, terms[t]));
                if (d < bestD) { bestD = d; bestE = e.id; bestT = static_cast<int>(t); }
            }
        }
        if (lmbDown && bestE) { ed.wireFromEntity = bestE; ed.wireFromTerm = bestT; }
        else if (lmbDown) pick();
        if (ed.wireFromEntity && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (bestE && bestE != ed.wireFromEntity) {
                Zone& z = ed.Z();
                if (!ed.wireLoose) {
                    z.links.push_back({ed.wireFromEntity, ed.wireFromTerm, bestE, bestT});
                    ed.Commit("Connected (in the wall)");
                } else {
                    // a real wire: a rope with a clip at each end, a bit longer than the gap so it hangs
                    const auto ta = ed.world.TerminalPositions(ed.wireFromEntity), tb = ed.world.TerminalPositions(bestE);
                    const Vec3 a = ta[ed.wireFromTerm], b = tb[bestT];
                    const float d = std::max(0.05f, Distance(a, b));
                    Entity w;
                    w.id = z.NextId();
                    w.name = "Wire_" + std::to_string(w.id);
                    w.type = "wire";
                    w.pos = (a + b) * 0.5f;
                    w.size = {0.008f, 0.008f, d};
                    const Vec3 dir = (b - a).Normalized();
                    w.rotDeg = {-std::asin(std::clamp(dir.y, -1.f, 1.f)) * 57.29578f, std::atan2(dir.x, dir.z) * 57.29578f, 0};
                    w.color = ed.wireColor;
                    w.params = {{"length", d * 1.15f}, {"resistance", ed.wireGauge * d * 1.15f}};
                    z.entities.push_back(w);
                    z.links.push_back({w.id, 0, ed.wireFromEntity, ed.wireFromTerm});
                    z.links.push_back({w.id, 1, bestE, bestT});
                    ed.Commit("Wired with a loose wire");
                }
            }
            ed.wireFromEntity = 0;
        }
        // right-click a connection to cut it
        if (rmbClick) {
            float bd = 8.f;
            int which = -1;
            for (size_t i = 0; i < ed.Z().links.size(); ++i) {
                const TerminalLink& l = ed.Z().links[i];
                const auto a = ed.world.TerminalPositions(l.entityA), b = ed.world.TerminalPositions(l.entityB);
                if (l.termA >= (int)a.size() || l.termB >= (int)b.size()) continue;
                const float d = SegDist2D(mouse, Screen(ed, a[l.termA]), Screen(ed, b[l.termB]));
                if (d < bd) { bd = d; which = static_cast<int>(i); }
            }
            if (which >= 0) { ed.Z().links.erase(ed.Z().links.begin() + which); ed.Commit("Wire cut"); }
        }
        break;
    }
    case Tool::Terrain: {
        Heightfield* hf = ed.world.TerrainField();
        if (!hf) { if (lmbDown) ed.Log("No terrain: add one in the World panel"); break; }
        if (lmbDown) {
            ed.stroking = true;
            ed.flattenY = ed.aim.y;
            if (ed.brush == Brush::Water) {
                // dig a hollow and fill it
                CarveBasin(*hf, ed.aim.x, ed.aim.z, ed.brushR, std::max(0.4f, ed.brushR * 0.3f));
                ed.world.TerrainChanged(0, true);
                Entity w;
                w.id = ed.Z().NextId();
                w.name = "Pond_" + std::to_string(w.id);
                w.type = "water";
                w.pos = ed.aim;
                w.size = {ed.brushR * 2.f, 4.f, ed.brushR * 2.f};
                w.color = {70, 110, 140};
                w.params = {{"autoLevel", true}, {"tempC", 15}};
                ed.Z().entities.push_back(w);
                ed.stroking = false;
                ed.Commit("Pond");
                break;
            }
        }
        if (ed.stroking && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const float s = ed.brushStrength * dt;
            switch (ed.brush) {
            case Brush::Raise: BrushRaise(*hf, ed.aim.x, ed.aim.z, ed.brushR, 1.5f * s); break;
            case Brush::Lower: BrushRaise(*hf, ed.aim.x, ed.aim.z, ed.brushR, -1.5f * s); break;
            case Brush::Smooth: BrushSmooth(*hf, ed.aim.x, ed.aim.z, ed.brushR, std::min(1.f, 8.f * s)); break;
            case Brush::Flatten: BrushFlatten(*hf, ed.aim.x, ed.aim.z, ed.brushR, ed.flattenY, std::min(1.f, 8.f * s)); break;
            case Brush::Noise:
                for (int k = 0; k < 6; ++k) {
                    const float a = GetRandomValue(0, 6283) / 1000.f, r = GetRandomValue(0, 1000) / 1000.f * ed.brushR;
                    BrushRaise(*hf, ed.aim.x + std::cos(a) * r, ed.aim.z + std::sin(a) * r, ed.brushR * 0.25f, (GetRandomValue(0, 1) ? 1.f : -1.f) * 0.8f * s);
                }
                break;
            default: break;
            }
            ed.world.TerrainChanged(0, false);
        }
        if (ed.stroking && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ed.stroking = false; ed.world.TerrainChanged(0, true); ed.Commit("Sculpted"); }
        break;
    }
    case Tool::Draw: {
        // first click sets the start on whatever is under the mouse; drag over the level plane through it
        if (lmbDown) {
            ed.drawing = true;
            ed.drawA = {SnapV(ed, ed.aim.x, shift), ed.aim.y, SnapV(ed, ed.aim.z, shift)};
            ed.drawB = ed.drawA;
        }
        if (ed.drawing) {
            const Vec3 p = PlanePoint(ed.mouseRay, ed.drawA.y, ed.drawB);
            ed.drawB = {SnapV(ed, p.x, shift), ed.drawA.y, SnapV(ed, p.z, shift)};
            if (ed.shape == DrawShape::Wall && !shift && ed.snapOn) {
                // walls lock to 0/45/90°... (hold Shift for free angles)
                const float dx = ed.drawB.x - ed.drawA.x, dz = ed.drawB.z - ed.drawA.z, l = std::hypot(dx, dz);
                const float ang = std::round(std::atan2(dz, dx) / (PI / 4.f)) * (PI / 4.f);
                const float ls = std::round(l / ed.snap) * ed.snap;
                ed.drawB = {ed.drawA.x + std::cos(ang) * ls, ed.drawA.y, ed.drawA.z + std::sin(ang) * ls};
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                ed.drawing = false;
                const char* names[] = {"wall", "floor", "box", "room", "stairs", "water", "ramp"};
                const int n = FinishDraw(ed);
                if (n) ed.Commit(std::string("Drew ") + names[static_cast<int>(ed.shape)]);
            }
            if (rmbClick) ed.drawing = false;
        }
        break;
    }
    case Tool::Scale: {
        Entity* e = ed.Sel();
        if (lmbDown) {
            ed.scaleFace = -1;
            if (Scalable(ed, e)) {
                float best = 11.f;
                for (int f = 0; f < 6; ++f) {
                    const float dd = Vector2Distance(mouse, Screen(ed, e->pos + FaceDir(*e, f) * FaceHalf(*e, f)));
                    if (dd < best) { best = dd; ed.scaleFace = f; }
                }
            }
            if (ed.scaleFace >= 0) {
                ed.scaleStartPos = e->pos;
                ed.scaleStartSize = e->size;
                ed.scaleStartMass = e->massKg;
                ed.scaleGrab = AxisParam(ed.mouseRay, e->pos, FaceDir(*e, ed.scaleFace));
            } else pick();
        }
        if (ed.scaleFace >= 0 && e && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            const Vec3 dir = FaceDir(*e, ed.scaleFace);
            float t = AxisParam(ed.mouseRay, ed.scaleStartPos, dir) - ed.scaleGrab;
            const int ax = ed.scaleFace / 2;
            const float s0 = ax == 0 ? ed.scaleStartSize.x : ax == 1 ? ed.scaleStartSize.y : ed.scaleStartSize.z;
            const bool both = ctrl;   // Ctrl: grow both sides (centre stays)
            float s1 = std::max(0.005f, s0 + (both ? 2.f : 1.f) * t);
            if (ed.snapOn && !shift) s1 = std::max(ed.snap, std::round(s1 / ed.snap) * ed.snap);
            Vec3 size = ed.scaleStartSize;
            (ax == 0 ? size.x : ax == 1 ? size.y : size.z) = s1;
            e->size = size;
            e->pos = both ? ed.scaleStartPos : ed.scaleStartPos + dir * ((s1 - s0) * 0.5f);
            if (ed.scaleStartMass > 0.f && s0 > 0.f) e->massKg = std::max(0.01f, ed.scaleStartMass * s1 / s0);   // same material, more of it
            ed.Rebuild();
        }
        if (ed.scaleFace >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ed.scaleFace = -1; ed.Commit("Resized"); }
        break;
    }
    case Tool::Rotate: {
        const Vec3 c = SelectionCenter(ed);
        const float rad = std::max(0.35f, Distance(ToEon(ed.fly.cam.position), c) * 0.17f);
        auto angleAt = [&]() { const Vec3 p = PlanePoint(ed.mouseRay, c.y, c + Vec3{rad, 0, 0}); return std::atan2(-(p.z - c.z), p.x - c.x) * RAD2DEG; };
        if (lmbDown) {
            ed.rotating = false;
            if (!ed.selection.empty()) {
                for (int k = 0; k < 48 && !ed.rotating; ++k) {
                    const float a = k / 48.f * 2.f * PI;
                    if (SegDist2D(mouse, Screen(ed, c + Vec3{std::cos(a) * rad, 0, std::sin(a) * rad}), Screen(ed, c + Vec3{std::cos(a + 0.131f) * rad, 0, std::sin(a + 0.131f) * rad})) < 9.f) ed.rotating = true;
                }
            }
            if (ed.rotating) {
                ed.rotCenter = c;
                ed.rotStartAngle = angleAt();
                ed.rotDelta = 0.f;
                ed.rotStart.clear();
                for (int id : ed.TopLevelSelection()) if (const Entity* e = ed.Z().Find(id)) ed.rotStart[id] = {e->pos, e->rotDeg.y};
            } else pick();
        }
        if (ed.rotating && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float dA = std::remainder(angleAt() - ed.rotStartAngle, 360.f);
            if (ed.snapOn && !shift) dA = std::round(dA / ed.rotSnap) * ed.rotSnap;
            ed.rotDelta = dA;
            const float a = dA * DEG2RAD, cs = std::cos(a), sn = std::sin(a);
            for (auto& [id, st] : ed.rotStart) {
                Entity* e = ed.Z().Find(id);
                if (!e) continue;
                const Vec3 r = st.first - ed.rotCenter;
                e->pos = ed.rotCenter + Vec3{r.x * cs + r.z * sn, r.y, -r.x * sn + r.z * cs};
                e->rotDeg.y = std::fmod(st.second + dA + 720.f, 360.f);
            }
            ed.Rebuild();
        }
        if (ed.rotating && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { ed.rotating = false; ed.Commit("Rotated " + std::to_string(static_cast<int>(std::lround(ed.rotDelta))) + "°"); }
        if (!typing && !ed.selection.empty() && IsKeyPressed(KEY_R)) {
            for (int id : ed.TopLevelSelection()) if (Entity* e = ed.Z().Find(id)) e->rotDeg.y = std::fmod(e->rotDeg.y + (shift ? -ed.rotSnap : ed.rotSnap) + 360.f, 360.f);
            ed.Commit("Rotated");
        }
        break;
    }
    case Tool::Paint:
        if (lmbDown && ed.hasHit) {
            Entity* h = ed.Z().Find(ed.hit.entity);
            if (!h) break;
            if (IsKeyDown(KEY_LEFT_ALT)) { ed.paintColor = h->color; ed.Log("Picked colour"); break; }   // eyedropper
            if (Entity* inst = ed.InstanceOf(h)) {
                // a part of an object: the colour goes into the instance's overrides
                const std::string part = h->name.substr(inst->name.size() + 1);
                if (!inst->params.contains("set") || !inst->params["set"].is_object()) inst->params["set"] = nlohmann::json::object();
                inst->params["set"][part + ".color"] = {ed.paintColor.r, ed.paintColor.g, ed.paintColor.b};
            } else h->color = ed.paintColor;
            ed.Commit("Painted " + h->name);
        }
        break;
    case Tool::Measure:
        if (lmbDown) {
            const Vec3 p = ed.aim;
            if (ed.measureStage != 1) { ed.measureA = p; ed.measureB = p; ed.measureStage = 1; }
            else { ed.measureB = p; ed.measureStage = 2; }
        }
        if (ed.measureStage == 1) ed.measureB = ed.aim;
        if (rmbClick) ed.measureStage = 0;
        break;
    case Tool::Plants:
        if (lmbDown) {
            if (shift) {
                auto& E = ed.Z().entities;
                const size_t before = E.size();
                E.erase(std::remove_if(E.begin(), E.end(), [&](const Entity& e) { return e.type == "plant" && std::hypot(e.pos.x - ed.aim.x, e.pos.z - ed.aim.z) < ed.brushR; }), E.end());
                ed.Commit("Cleared " + std::to_string(before - E.size()) + " plants");
            } else {
                const int n = ed.world.ScatterPlants(kSpecies[ed.plantSpecies], ed.aim, ed.brushR, ed.plantDensity, ed.scatterSeed++, 1.f);
                ed.Commit("Planted " + std::to_string(n) + " " + kSpecies[ed.plantSpecies]);
            }
        }
        break;
    }
}

void DrawViewportOverlay(Editor& ed) {
    bool hasTerrain = ed.world.TerrainField() != nullptr;
    if (!ed.playing && !hasTerrain) DrawGrid(40, 0.5f);
    // selection boxes
    for (int id : ed.selection) {
        const Entity* e = ed.Z().Find(id);
        if (!e || e->type == "start") continue;   // drawn as its own marker
        Vec3 axis;
        float ang = 0.f;
        Quat::FromEulerDeg(e->rotDeg).ToAxisAngle(axis, ang);
        const Vec3 s = e->type == "prefab" ? Vec3{0.3f, 0.3f, 0.3f} : e->size;
        rlPushMatrix();
        rlTranslatef(e->pos.x, e->pos.y, e->pos.z);
        rlRotatef(ang * RAD2DEG, axis.x, axis.y, axis.z);
        DrawCubeWires({0, 0, 0}, s.x + 0.01f, s.y + 0.01f, s.z + 0.01f, id == ed.primary ? YELLOW : ORANGE);
        rlPopMatrix();
    }
    // move gizmo
    if (ed.tool == Tool::Move && !ed.selection.empty() && !ed.playing) {
        const Vec3 c = SelectionCenter(ed);
        const float len = std::max(0.3f, Distance(ToEon(ed.fly.cam.position), c) * 0.15f);
        const Color cols[3] = {RED, GREEN, BLUE};
        const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        rlDisableDepthTest();
        for (int a = 0; a < 3; ++a) {
            const Vec3 tip = c + axes[a] * len;
            const Color col = ed.gizmoAxis == a ? YELLOW : cols[a];
            DrawCylinderEx(ToRl(c), ToRl(tip), len * 0.02f, len * 0.02f, 6, col);
            DrawCylinderEx(ToRl(tip), ToRl(tip + axes[a] * (len * 0.18f)), len * 0.07f, 0.f, 8, col);
        }
        rlEnableDepthTest();
    }
    // wiring
    if (ed.tool == Tool::Wire || ed.showSchematic) {
        for (const TerminalLink& l : ed.Z().links) {
            const auto a = ed.world.TerminalPositions(l.entityA), b = ed.world.TerminalPositions(l.entityB);
            if (l.termA >= (int)a.size() || l.termB >= (int)b.size()) continue;
            const Entity* ea = ed.Z().Find(l.entityA);
            const bool rope = ea && ea->type == "wire" && !ea->params.value("hidden", false);
            if (rope) continue;   // the real wire is drawn by the world
            const Part* p = ed.world.PartOf(l.entityA);
            const bool live = p && std::fabs(p->i) > 1e-4;
            if (ed.tool == Tool::Wire || live) DrawLine3D(ToRl(a[l.termA]), ToRl(b[l.termB]), live ? Color{255, 200, 60, 255} : Color{230, 230, 120, 160});
        }
    }
    if (ed.tool == Tool::Wire && !ed.playing) {
        for (const Entity& e : ed.Z().entities) {
            if (e.part < 0) continue;
            for (const Vec3& t : ed.world.TerminalPositions(e.id)) DrawSphere(ToRl(t), 0.012f, {90, 230, 90, 255});
        }
        if (ed.wireFromEntity) {
            const auto t = ed.world.TerminalPositions(ed.wireFromEntity);
            if (ed.wireFromTerm >= 0 && ed.wireFromTerm < (int)t.size()) DrawLine3D(ToRl(t[ed.wireFromTerm]), ToRl(ed.aim), ed.wireLoose ? Color{230, 110, 70, 255} : Color{255, 255, 120, 255});
        }
    }
    // placing ghost
    if (ed.tool == Tool::Place && ed.placingActive && ed.viewHovered) {
        const Vec3 s = ed.placing.type == "prefab" || ed.placing.type == "plant" ? Vec3{0.3f, 0.3f, 0.3f} : ed.placing.size;
        DrawCubeWiresV(ToRl(ed.aim + Vec3{0, 0.5f * s.y, 0}), ToRl(s), SKYBLUE);
    }
    if (ed.playing) return;
    // Jenny's start: a figure-sized frame with an arrow the way she'll face
    for (const Entity& e : ed.Z().entities) {
        if (e.type != "start") continue;
        const Color c = ed.selection.count(e.id) ? YELLOW : Color{255, 120, 200, 255};
        float g = e.pos.y;
        RayHit hit;
        if (ed.world.RayCast(e.pos + Vec3{0, 0.4f, 0}, {0, -1, 0}, 6.f, hit, true)) g = hit.point.y;
        const Vec3 f{e.pos.x, g, e.pos.z};
        DrawCylinderWiresEx(ToRl(f), ToRl(f + Vec3{0, 1.68f, 0}), 0.18f, 0.18f, 10, c);
        DrawSphereWires(ToRl(f + Vec3{0, 1.55f, 0}), 0.11f, 6, 8, c);
        const float a = e.rotDeg.y * DEG2RAD;
        const Vec3 fwd{std::sin(a), 0, std::cos(a)};   // her forward is local +z
        DrawCylinderEx(ToRl(f + Vec3{0, 0.02f, 0}), ToRl(f + Vec3{0, 0.02f, 0} + fwd * 0.45f), 0.02f, 0.02f, 6, c);
        DrawCylinderEx(ToRl(f + Vec3{0, 0.02f, 0} + fwd * 0.45f), ToRl(f + Vec3{0, 0.02f, 0} + fwd * 0.6f), 0.06f, 0.f, 8, c);
        if (std::fabs(g - e.pos.y) > 0.02f) DrawLine3D(ToRl(e.pos), ToRl(f), c);
    }
    // draw tool: the shape being dragged
    if (ed.tool == Tool::Draw && ed.drawing) {
        const Vec3 a = ed.drawA, b = ed.drawB;
        const Color col{120, 200, 255, 255};
        if (ed.shape == DrawShape::Wall || ed.shape == DrawShape::Stairs || ed.shape == DrawShape::Ramp) {
            DrawLine3D(ToRl(a + Vec3{0, 0.01f, 0}), ToRl(b + Vec3{0, 0.01f, 0}), col);
            DrawLine3D(ToRl(a + Vec3{0, ed.drawH, 0}), ToRl(b + Vec3{0, ed.shape == DrawShape::Wall ? ed.drawH : ed.drawH, 0}), col);
            DrawLine3D(ToRl(a), ToRl(a + Vec3{0, ed.shape == DrawShape::Wall ? ed.drawH : 0.f, 0}), col);
            DrawLine3D(ToRl(b), ToRl(b + Vec3{0, ed.drawH, 0}), col);
        } else {
            const float h = ed.shape == DrawShape::Floor ? ed.drawT : ed.shape == DrawShape::Water ? -ed.drawH : ed.drawH;
            const Vec3 c{(a.x + b.x) * 0.5f, a.y + h * 0.5f, (a.z + b.z) * 0.5f};
            DrawCubeWiresV(ToRl(c), {std::fabs(b.x - a.x), std::fabs(h), std::fabs(b.z - a.z)}, col);
        }
        DrawSphere(ToRl(a), 0.03f, col);
    }
    if (ed.tool == Tool::Draw && !ed.drawing && ed.viewHovered) {
        const Vec3 p{ed.snapOn ? std::round(ed.aim.x / ed.snap) * ed.snap : ed.aim.x, ed.aim.y, ed.snapOn ? std::round(ed.aim.z / ed.snap) * ed.snap : ed.aim.z};
        DrawLine3D(ToRl(p - Vec3{0.15f, 0, 0}), ToRl(p + Vec3{0.15f, 0, 0}), SKYBLUE);
        DrawLine3D(ToRl(p - Vec3{0, 0, 0.15f}), ToRl(p + Vec3{0, 0, 0.15f}), SKYBLUE);
    }
    // scale handles
    if (ed.tool == Tool::Scale) {
        const Entity* e = ed.Sel();
        if (Scalable(ed, e)) {
            rlDisableDepthTest();
            const float k = std::max(0.015f, Distance(ToEon(ed.fly.cam.position), e->pos) * 0.012f);
            const Color cols[3] = {RED, GREEN, BLUE};
            for (int f = 0; f < 6; ++f) DrawCubeV(ToRl(e->pos + FaceDir(*e, f) * FaceHalf(*e, f)), {k, k, k}, ed.scaleFace == f ? YELLOW : cols[f / 2]);
            rlEnableDepthTest();
        }
    }
    // rotate ring
    if (ed.tool == Tool::Rotate && !ed.selection.empty()) {
        const Vec3 c = ed.rotating ? ed.rotCenter : SelectionCenter(ed);
        const float rad = std::max(0.35f, Distance(ToEon(ed.fly.cam.position), c) * 0.17f);
        rlDisableDepthTest();
        for (int k = 0; k < 64; ++k) {
            const float a0 = k / 64.f * 2.f * PI, a1 = (k + 1) / 64.f * 2.f * PI;
            DrawCylinderEx(ToRl(c + Vec3{std::cos(a0) * rad, 0, std::sin(a0) * rad}), ToRl(c + Vec3{std::cos(a1) * rad, 0, std::sin(a1) * rad}), rad * 0.012f, rad * 0.012f, 4, ed.rotating ? YELLOW : GREEN);
        }
        if (ed.rotating) {
            const float a = (ed.rotStartAngle + ed.rotDelta) * DEG2RAD;
            DrawLine3D(ToRl(c), ToRl(c + Vec3{std::cos(a) * rad, 0, -std::sin(a) * rad}), YELLOW);
        }
        rlEnableDepthTest();
    }
    // measure
    if (ed.tool == Tool::Measure && ed.measureStage > 0) {
        rlDisableDepthTest();
        DrawLine3D(ToRl(ed.measureA), ToRl(ed.measureB), YELLOW);
        DrawSphere(ToRl(ed.measureA), 0.015f, YELLOW);
        DrawSphere(ToRl(ed.measureB), 0.015f, YELLOW);
        DrawLine3D(ToRl(ed.measureA), ToRl({ed.measureB.x, ed.measureA.y, ed.measureB.z}), {255, 255, 120, 110});
        rlEnableDepthTest();
    }
    // brush circle on the ground
    if ((ed.tool == Tool::Terrain || ed.tool == Tool::Plants) && ed.viewHovered) {
        const Color c = ed.tool == Tool::Terrain ? ORANGE : LIME;
        for (int k = 0; k < 64; ++k) {
            const float a0 = k / 64.f * 6.2831853f, a1 = (k + 1) / 64.f * 6.2831853f;
            Vec3 p0{ed.aim.x + std::cos(a0) * ed.brushR, ed.aim.y, ed.aim.z + std::sin(a0) * ed.brushR};
            Vec3 p1{ed.aim.x + std::cos(a1) * ed.brushR, ed.aim.y, ed.aim.z + std::sin(a1) * ed.brushR};
            float g;
            if (ed.world.GroundHeight(p0.x, p0.z, g)) p0.y = g;
            if (ed.world.GroundHeight(p1.x, p1.z, g)) p1.y = g;
            DrawLine3D(ToRl(p0 + Vec3{0, 0.04f, 0}), ToRl(p1 + Vec3{0, 0.04f, 0}), c);
        }
    }
}

void DrawViewportHud(Editor& ed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto label = [&](const Vec3& at, const std::string& t) {
        const Vector2 p = Screen(ed, at);
        const ImVec2 sz = ImGui::CalcTextSize(t.c_str());
        dl->AddRectFilled({p.x + 10, p.y - 4}, {p.x + 18 + sz.x, p.y + sz.y}, IM_COL32(20, 20, 24, 200), 3.f);
        dl->AddText({p.x + 14, p.y - 2}, IM_COL32(255, 240, 150, 255), t.c_str());
    };
    char buf[160];
    if (ed.tool == Tool::Draw && ed.drawing) {
        const float dx = std::fabs(ed.drawB.x - ed.drawA.x), dz = std::fabs(ed.drawB.z - ed.drawA.z);
        if (ed.shape == DrawShape::Wall || ed.shape == DrawShape::Stairs || ed.shape == DrawShape::Ramp) std::snprintf(buf, sizeof buf, "%.2f m long, %.2f m high", std::hypot(dx, dz), ed.drawH);
        else std::snprintf(buf, sizeof buf, "%.2f x %.2f m", dx, dz);
        label(ed.drawB, buf);
    }
    if (ed.tool == Tool::Scale) if (const Entity* e = ed.Sel(); Scalable(ed, e)) {
        std::snprintf(buf, sizeof buf, "%.3f x %.3f x %.3f m%s", e->size.x, e->size.y, e->size.z, e->massKg > 0.f ? "" : "");
        label(e->pos + Vec3{0, 0.5f * e->size.y, 0}, buf);
    }
    if (ed.tool == Tool::Rotate && ed.rotating) { std::snprintf(buf, sizeof buf, "%+.0f°", ed.rotDelta); label(ed.rotCenter, buf); }
    if (ed.tool == Tool::Measure && ed.measureStage > 0) {
        const Vec3 d = ed.measureB - ed.measureA;
        std::snprintf(buf, sizeof buf, "%.3f m  (x %.2f  y %.2f  z %.2f, flat %.2f)", d.Length(), d.x, d.y, d.z, std::hypot(d.x, d.z));
        label((ed.measureA + ed.measureB) * 0.5f, buf);
    }
    // what the current tool does, along the bottom of the view
    const char* hint = "";
    switch (ed.tool) {
    case Tool::Select: hint = "Select: click (Ctrl adds, Alt picks a part inside an object)   F frame   End drop to floor   Ctrl+C/V copy/paste"; break;
    case Tool::Move: hint = "Move: drag an arrow (Shift: no snap)   R / Shift+R turn 15°"; break;
    case Tool::Wire: hint = "Wire: drag terminal to terminal   right-click a wire to cut it"; break;
    case Tool::Terrain: hint = "Terrain: hold LMB to sculpt (brush in the World panel)"; break;
    case Tool::Plants: hint = "Plants: LMB plant   Shift+LMB clear"; break;
    case Tool::Place: hint = "Place: click to put it down (Shift keeps placing)   R / Shift+R turn it   Esc stop"; break;
    case Tool::Draw: hint = "Draw: drag on the ground (Shift: free, no snap)   right-click cancels   shape and height in the toolbar"; break;
    case Tool::Scale: hint = "Scale: drag a face handle (Ctrl: both sides, Shift: no snap)"; break;
    case Tool::Rotate: hint = "Rotate: drag the ring (Shift: free)   R / Shift+R 15° steps"; break;
    case Tool::Paint: hint = "Paint: click to paint   Alt+click picks a colour"; break;
    case Tool::Measure: hint = "Measure: click two points   right-click clears"; break;
    }
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddText({wp.x + 10, wp.y + ws.y - 22}, IM_COL32(230, 230, 230, 170), hint);
}

} // namespace eon
