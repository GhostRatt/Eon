// Editor actions on the selection: drop to floor, align, distribute, array, copy/paste, frame.
#include <algorithm>
#include <cmath>

#include "raylib.h"
#include "raymath.h"

#include "Editor.h"

namespace eon {

namespace {
// world-space half extents of an entity's rotated box (an object instance counts as a point at its base)
Vec3 HalfAabb(const Entity& e) {
    if (e.type == "prefab" || e.type == "plant") return {0, 0, 0};
    const Quat q = Quat::FromEulerDeg(e.rotDeg);
    const Vec3 h = e.size * 0.5f;
    const Vec3 X = q.Rotate({1, 0, 0}), Y = q.Rotate({0, 1, 0}), Zz = q.Rotate({0, 0, 1});
    return {std::fabs(X.x) * h.x + std::fabs(Y.x) * h.y + std::fabs(Zz.x) * h.z,
            std::fabs(X.y) * h.x + std::fabs(Y.y) * h.y + std::fabs(Zz.y) * h.z,
            std::fabs(X.z) * h.x + std::fabs(Y.z) * h.y + std::fabs(Zz.z) * h.z};
}
float Comp(const Vec3& v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
void SetComp(Vec3& v, int a, float x) { (a == 0 ? v.x : a == 1 ? v.y : v.z) = x; }
// an object instance's origin is its base, so its "centre" in y is the origin
float Lo(const Entity& e, int a) { return Comp(e.pos, a) - Comp(HalfAabb(e), a); }
float Hi(const Entity& e, int a) { return Comp(e.pos, a) + Comp(HalfAabb(e), a); }

std::string UniqueName(const Zone& z, const std::string& want) {
    if (!z.FindByName(want)) return want;
    std::string base = want;
    const size_t us = base.find_last_of('_');
    if (us != std::string::npos && us + 1 < base.size() && std::all_of(base.begin() + us + 1, base.end(), ::isdigit)) base = base.substr(0, us);
    for (int i = 2;; ++i) {
        const std::string n = base + "_" + std::to_string(i);
        if (!z.FindByName(n)) return n;
    }
}

// copies of `ids` (and the wiring among them) moved by `offset` and turned by `yaw` about `pivot`
std::vector<int> CopyInto(Zone& z, const std::vector<Entity>& src, const std::vector<TerminalLink>& links, const Vec3& offset, float yaw, const Vec3& pivot) {
    std::map<int, int> remap;
    std::vector<Entity> add;
    int next = z.NextId();
    const float a = yaw * DEG2RAD, c = std::cos(a), s = std::sin(a);
    for (const Entity& e : src) {
        Entity n = e;
        n.id = next++;
        n.name = UniqueName(z, e.name);
        const Vec3 r = e.pos - pivot;
        n.pos = pivot + Vec3{r.x * c + r.z * s, r.y, -r.x * s + r.z * c} + offset;
        n.rotDeg.y = std::fmod(e.rotDeg.y + yaw + 720.f, 360.f);
        remap[e.id] = n.id;
        z.entities.push_back(n);   // names must be unique before the next copy picks its own
        add.push_back(n);
    }
    for (const TerminalLink& l : links)
        if (remap.count(l.entityA) && remap.count(l.entityB)) z.links.push_back({remap[l.entityA], l.termA, remap[l.entityB], l.termB});
    std::vector<int> ids;
    for (const Entity& e : add) ids.push_back(e.id);
    return ids;
}
}   // namespace

std::vector<int> Editor::TopLevelSelection() {
    std::vector<int> out;
    for (int id : selection) if (const Entity* e = Z().Find(id); e && !InstanceOf(e) && e->type != "terrain") out.push_back(id);
    return out;
}

void Editor::DropToFloor() {
    int moved = 0;
    for (int id : TopLevelSelection()) {
        Entity* e = Z().Find(id);
        if (!e) continue;
        const Vec3 h = HalfAabb(*e);
        const float bottom = e->pos.y - h.y;
        // the highest surface under its footprint (centre and four corners)
        float best = -1e9f;
        const Vec3 pts[5] = {{0, 0, 0}, {h.x * 0.9f, 0, h.z * 0.9f}, {-h.x * 0.9f, 0, h.z * 0.9f}, {h.x * 0.9f, 0, -h.z * 0.9f}, {-h.x * 0.9f, 0, -h.z * 0.9f}};
        for (const Vec3& o : pts) {
            RayHit hit;
            const Vec3 from{e->pos.x + o.x, bottom - 0.003f, e->pos.z + o.z};
            if (world.RayCast(from, {0, -1, 0}, 100.f, hit) && hit.entity != id) best = std::max(best, hit.point.y);
            float g;
            if (world.GroundHeight(from.x, from.z, g) && g <= from.y + 0.01f) best = std::max(best, g);
        }
        if (best < -1e8f) best = 0.f;
        const float lift = e->type == "door" ? 0.012f : 0.001f;
        if (std::fabs(best + lift - bottom) > 1e-4f) { e->pos.y += best + lift - bottom; ++moved; }
    }
    Commit("Dropped " + std::to_string(moved) + " to the floor");
}

void Editor::Align(int axis, int mode) {
    const Entity* p = Sel();
    if (!p || selection.size() < 2) { Log("Align: select two or more (the yellow one stays put)"); return; }
    const float target = mode == 0 ? Lo(*p, axis) : mode == 2 ? Hi(*p, axis) : Comp(p->pos, axis);
    for (int id : TopLevelSelection()) {
        if (id == primary) continue;
        Entity* e = Z().Find(id);
        const float cur = mode == 0 ? Lo(*e, axis) : mode == 2 ? Hi(*e, axis) : Comp(e->pos, axis);
        SetComp(e->pos, axis, Comp(e->pos, axis) + target - cur);
    }
    Commit(std::string("Aligned ") + "XYZ"[axis]);
}

void Editor::Distribute(int axis) {
    std::vector<Entity*> es;
    for (int id : TopLevelSelection()) es.push_back(Z().Find(id));
    if (es.size() < 3) { Log("Distribute: select three or more"); return; }
    std::sort(es.begin(), es.end(), [&](Entity* a, Entity* b) { return Comp(a->pos, axis) < Comp(b->pos, axis); });
    const float a = Comp(es.front()->pos, axis), b = Comp(es.back()->pos, axis);
    for (size_t i = 1; i + 1 < es.size(); ++i) SetComp(es[i]->pos, axis, a + (b - a) * i / (es.size() - 1));
    Commit(std::string("Distributed along ") + "XYZ"[axis]);
}

void Editor::ArraySelection(int count, const Vec3& offset, float yawStep) {
    std::vector<Entity> src;
    for (int id : TopLevelSelection()) src.push_back(*Z().Find(id));
    if (src.empty() || count < 2) return;
    Vec3 pivot{};
    for (const Entity& e : src) pivot = pivot + e.pos * (1.f / src.size());
    const std::vector<TerminalLink> links = Z().links;
    std::set<int> all(selection);
    for (int i = 1; i < count; ++i)
        for (int id : CopyInto(Z(), src, links, offset * static_cast<float>(i), yawStep * i, pivot)) all.insert(id);
    selection = all;
    Commit("Array of " + std::to_string(count));
}

void Editor::Duplicate() {
    std::vector<Entity> src;
    for (int id : TopLevelSelection()) src.push_back(*Z().Find(id));
    if (src.empty()) return;
    const auto ids = CopyInto(Z(), src, Z().links, {snapOn ? std::max(snap, 0.25f) : 0.25f, 0, 0}, 0.f, {});
    selection = std::set<int>(ids.begin(), ids.end());
    primary = ids.front();
    Commit("Duplicated " + std::to_string(ids.size()));
}

void Editor::Copy() {
    Zone tmp;
    std::set<int> ids;
    for (int id : TopLevelSelection()) { tmp.entities.push_back(*Z().Find(id)); ids.insert(id); }
    for (const TerminalLink& l : Z().links) if (ids.count(l.entityA) && ids.count(l.entityB)) tmp.links.push_back(l);
    if (tmp.entities.empty()) return;
    clipboard = ZoneToJson(tmp).dump();
    SetClipboardText(clipboard.c_str());
    Log("Copied " + std::to_string(tmp.entities.size()));
}

void Editor::Paste(const Vec3& at) {
    std::string text = clipboard;
    if (const char* sys = GetClipboardText(); sys && sys[0] == '{') text = sys;   // also from another editor window
    Zone tmp;
    nlohmann::json j;
    try { j = nlohmann::json::parse(text); } catch (...) { Log("Nothing to paste"); return; }
    if (!ZoneFromJson(j, tmp) || tmp.entities.empty()) { Log("Nothing to paste"); return; }
    Vec3 c{};
    float bottom = 1e9f;
    for (const Entity& e : tmp.entities) { c = c + e.pos * (1.f / tmp.entities.size()); bottom = std::min(bottom, Lo(e, 1)); }
    Vec3 off{at.x - c.x, at.y - bottom, at.z - c.z};
    if (snapOn) { off.x = std::round(off.x / snap) * snap; off.z = std::round(off.z / snap) * snap; }
    const auto ids = CopyInto(Z(), tmp.entities, tmp.links, off, 0.f, c);
    selection = std::set<int>(ids.begin(), ids.end());
    primary = ids.empty() ? -1 : ids.front();
    Commit("Pasted " + std::to_string(ids.size()));
}

void Editor::FrameSelection() {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    int n = 0;
    for (int id : selection) {
        const Entity* e = Z().Find(id);
        if (!e) continue;
        const Vec3 h = e->type == "prefab" ? Vec3{0.5f, 1.f, 0.5f} : HalfAabb(*e);
        const Vec3 c = e->type == "prefab" ? e->pos + Vec3{0, 0.8f, 0} : e->pos;
        lo = {std::min(lo.x, c.x - h.x), std::min(lo.y, c.y - h.y), std::min(lo.z, c.z - h.z)};
        hi = {std::max(hi.x, c.x + h.x), std::max(hi.y, c.y + h.y), std::max(hi.z, c.z + h.z)};
        ++n;
    }
    if (!n) return;
    const Vec3 c = (lo + hi) * 0.5f;
    const float r = std::max(0.3f, Distance(lo, hi) * 0.5f);
    Vec3 back = ToEon(fly.cam.position) - ToEon(fly.cam.target);
    back = back.Length() < 1e-3f ? Vec3{1, 1, 1} : back;
    fly.Init(ToRl(c + back.Normalized() * (r * 2.6f + 0.4f)), ToRl(c));
}

} // namespace eon
