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
}   // namespace

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
        if (ctrl && IsKeyPressed(KEY_N)) ed.showNewMap = true;
        if (ctrl && IsKeyPressed(KEY_O)) { ed.RefreshContent(); ed.showOpen = true; }
        if (ctrl && IsKeyPressed(KEY_A)) { ed.selection.clear(); for (const Entity& e : ed.Z().entities) if (!e.params.contains("_from") && e.type != "plant") ed.selection.insert(e.id); }
        if (IsKeyPressed(KEY_DELETE) && !ed.playing) ed.DeleteSelection();
        if (IsKeyPressed(KEY_ESCAPE)) { ed.tool = Tool::Select; ed.placingActive = false; ed.wireFromEntity = 0; }
        if (!ctrl && !ed.looking) {
            if (IsKeyPressed(KEY_ONE)) ed.tool = Tool::Select;
            if (IsKeyPressed(KEY_TWO)) ed.tool = Tool::Move;
            if (IsKeyPressed(KEY_THREE)) ed.tool = Tool::Wire;
            if (IsKeyPressed(KEY_FOUR)) ed.tool = Tool::Terrain;
            if (IsKeyPressed(KEY_FIVE)) ed.tool = Tool::Plants;
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
    if (!ed.viewHovered && !ed.stroking && ed.gizmoAxis < 0 && ed.wireFromEntity == 0) return;
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
        // rotate the selection 15° with R / F
        if (!typing && !ed.selection.empty() && (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_F)) && !ed.looking) {
            for (int id : ed.selection) if (Entity* e = ed.Z().Find(id); e && !ed.InstanceOf(e)) e->rotDeg.y = std::fmod(e->rotDeg.y + (IsKeyPressed(KEY_R) ? ed.rotSnap : -ed.rotSnap) + 360.f, 360.f);
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
    if (ed.tool != Tool::Select && ed.tool != Tool::Move && ed.tool != Tool::Wire && ed.tool != Tool::Place && ed.tool != Tool::Terrain && ed.tool != Tool::Plants) ed.tool = Tool::Select;
}

void DrawViewportOverlay(Editor& ed) {
    bool hasTerrain = ed.world.TerrainField() != nullptr;
    if (!ed.playing && !hasTerrain) DrawGrid(40, 0.5f);
    // selection boxes
    for (int id : ed.selection) {
        const Entity* e = ed.Z().Find(id);
        if (!e) continue;
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

} // namespace eon
