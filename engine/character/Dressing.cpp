#include "Dressing.h"

#include <algorithm>
#include <cmath>

namespace eon {

GarmentKind KindOf(const Garment& g) {
    auto has = [&](const char* r) { return std::find(g.covers.begin(), g.covers.end(), r) != g.covers.end(); };
    if (has("feet")) return GarmentKind::Feet;
    if (has("groin") || has("hips") || has("thighs") || has("legs")) return has("chest") ? GarmentKind::Top : GarmentKind::Bottom;
    return GarmentKind::Top;
}

void Dressing::Path(const Character& ch, GarmentKind k, int side, std::vector<Vec3>& out) const {
    out.clear();
    const RigMap& r = ch.Rig();
    const Vec3 fwd = ch.placement.r.Rotate({0, 0, 1});
    const Vec3 pelvis = ch.BonePos(r.pelvis);
    if (k == GarmentKind::Bottom) {
        // waistband at the side of her hip, then down the outside of that leg: knee, ankle, past the foot
        const Vec3 hip = ch.BonePos(r.thigh[side]);
        Vec3 out1 = hip - pelvis;
        out1.y = 0.f;
        const Vec3 lat = out1.Normalized({1, 0, 0});
        out.push_back(hip + lat * 0.09f + Vec3{0, 0.06f, 0});
        out.push_back(ch.BonePos(r.calf[side]) + lat * 0.07f);
        out.push_back(ch.BonePos(r.foot[side]) + lat * 0.05f + Vec3{0, 0.02f, 0});
        out.push_back(ch.BonePos(r.foot[side]) + fwd * 0.1f + Vec3{0, 0.03f, 0});   // over the toes: stepped out of
    } else if (k == GarmentKind::Top) {
        // the band at her side, under the arm; it comes away forward and down off her chest
        const int chest = r.spine[2] >= 0 ? r.spine[2] : r.SpineTop();
        const Vec3 c = ch.BonePos(chest);
        Vec3 lat = ch.BonePos(r.upperarm[side]) - c;
        lat.y = 0.f;
        lat = lat.Normalized({1, 0, 0});
        const Vec3 band = c + lat * 0.13f + fwd * 0.05f - Vec3{0, 0.04f, 0};
        out.push_back(band);
        out.push_back(band + fwd * 0.18f - lat * 0.03f);
        out.push_back(band + fwd * 0.36f - lat * 0.05f - Vec3{0, 0.06f, 0});
    } else {
        const Vec3 foot = ch.BonePos(r.foot[side]);
        out.push_back(foot + Vec3{0, 0.06f, 0} - fwd * 0.04f);
        out.push_back(foot + fwd * 0.25f + Vec3{0, 0.04f, 0});
    }
}

float Dressing::Along(const std::vector<Vec3>& path, const Vec3& p, float* dist) const {
    float total = 0.f;
    for (size_t i = 1; i < path.size(); ++i) total += (path[i] - path[i - 1]).Length();
    float best = 1e9f, bestT = 0.f, run = 0.f;
    for (size_t i = 1; i < path.size(); ++i) {
        const Vec3 a = path[i - 1], ab = path[i] - a;
        const float len = ab.Length();
        const float u = len > 1e-5f ? std::clamp((p - a).Dot(ab) / (len * len), 0.f, 1.f) : 0.f;
        const float d = (a + ab * u - p).Length();
        if (d < best) { best = d; bestT = (run + u * len) / std::max(total, 1e-5f); }
        run += len;
    }
    if (dist) *dist = best;
    return bestT;
}

Vec3 Dressing::At(const std::vector<Vec3>& path, float t) const {
    float total = 0.f;
    for (size_t i = 1; i < path.size(); ++i) total += (path[i] - path[i - 1]).Length();
    float want = std::clamp(t, 0.f, 1.f) * total;
    for (size_t i = 1; i < path.size(); ++i) {
        const float len = (path[i] - path[i - 1]).Length();
        if (want <= len || i + 1 == path.size()) return path[i - 1] + (path[i] - path[i - 1]) * (len > 1e-5f ? std::min(1.f, want / len) : 0.f);
        want -= len;
    }
    return path.empty() ? Vec3{} : path.back();
}

int Dressing::SpawnObject(World& world, const Garment& g, const Vec3& pos) {
    // a light, soft bundle of cloth (what it is stays engine-side: Jenny only sees a small soft thing)
    Entity e;
    e.type = "cloth";
    e.name = "cloth";
    const bool top = KindOf(g) == GarmentKind::Top;
    e.size = top ? Vec3{0.16f, 0.03f, 0.09f} : Vec3{0.11f, 0.025f, 0.09f};
    e.pos = pos;
    e.massKg = top ? 0.05f : 0.03f;
    e.color = g.color;
    e.params = {{"material", "soft"}};
    return world.Spawn(e);
}

void Dressing::PlaceOff(World& world, Wardrobe& wardrobe, int g, const Vec3& pos) {
    if (g < 0 || g >= (int)wardrobe.garments.size()) return;
    if (st_.size() < wardrobe.garments.size()) st_.resize(wardrobe.garments.size());
    wardrobe.garments[g].worn = false;
    wardrobe.garments[g].off = 0.f;
    if (st_[g].entity) world.Despawn(st_[g].entity);
    st_[g].entity = SpawnObject(world, wardrobe.garments[g], pos);
}

void Dressing::Step(World& world, Character& ch, Motor& motor, const MotorIntent& in, Wardrobe& wardrobe, float dt) {
    (void)dt;
    feel_.clear();
    if (st_.size() < wardrobe.garments.size()) st_.resize(wardrobe.garments.size());
    Vec3 palm[2];
    bool gripRise[2];
    for (int i = 0; i < 2; ++i) {
        palm[i] = motor.HandBody(i) >= 0 ? world.BodyPosition(motor.HandBody(i)) : ch.BonePos(ch.Rig().hand[i]);
        gripRise[i] = in.hands[i].grip && !wasGrip_[i];
        if (!in.hands[i].grip) for (St& s : st_) s.hand[i] = false;   // letting go lets go of the garment too
    }
    std::vector<Vec3> path;
    for (int g = 0; g < (int)wardrobe.garments.size(); ++g) {
        Garment& gm = wardrobe.garments[g];
        St& s = st_[g];
        const GarmentKind kind = KindOf(gm);

        if (gm.worn) {
            // take hold of it where it can be held, at the point it has slid to
            for (int i = 0; i < 2; ++i) {
                if (s.hand[i] || !gripRise[i] || motor.HeldBody(i) >= 0) continue;
                if (gm.off <= 0.f && !wardrobe.CanRemove(g)) continue;   // something's over it (or streamer mode)
                Path(ch, kind, i, path);
                float d = 0.f;
                Along(path, palm[i], &d);
                if ((At(path, gm.off) - palm[i]).Length() < grabReach || (d < grabReach * 0.7f && gm.off > 0.f)) s.hand[i] = true;
            }
            // held: it goes where her hands take it along the way it slides
            if (s.hand[0] || s.hand[1]) {
                float lo = 1.f, hi = 0.f;
                int n = 0;
                for (int i = 0; i < 2; ++i) {
                    if (!s.hand[i]) continue;
                    Path(ch, kind, i, path);
                    float d = 0.f;
                    const float t = Along(path, palm[i], &d);
                    if (d > 0.25f) { s.hand[i] = false; continue; }   // her hand left the garment's way: it slipped out
                    lo = std::min(lo, t);
                    hi = std::max(hi, t);
                    ++n;
                }
                if (n > 0) {
                    float t = 0.7f * hi + 0.3f * lo;   // the leading hand drags it, the other side follows
                    // one hand can't move a waistband as fast as two (the other side lags)
                    if (n == 1 && kind == GarmentKind::Bottom) t = gm.off + (t - gm.off) * 0.6f;
                    gm.off = std::clamp(t, 0.f, 1.f);
                }
                if (gm.off >= 0.94f) {
                    // off: now it's a thing in her hand
                    const int hand = s.hand[1] ? 1 : 0;
                    gm.worn = false;
                    gm.off = 0.f;
                    s.hand[0] = s.hand[1] = false;
                    if (s.entity) world.Despawn(s.entity);
                    s.entity = SpawnObject(world, gm, palm[hand] + Vec3{0, -0.03f, 0});
                    s.justOff = true;   // it can't go straight back on until she's let go of it once
                    feel_.push_back({"it came off", 1.f});
                } else if (gm.off <= 0.02f && (s.hand[0] || s.hand[1])) {
                    gm.off = 0.f;
                }
            }
        } else if (s.entity) {
            // dressing: holding it at the far end of its way (her feet, in front of her chest), it starts going on
            const int body = world.MainBodyOf(s.entity);
            if (s.justOff && motor.HeldBody(0) != body && motor.HeldBody(1) != body && !in.hands[0].grip && !in.hands[1].grip) s.justOff = false;
            for (int i = 0; i < 2 && body >= 0 && !s.justOff; ++i) {
                if (motor.HeldBody(i) != body) continue;
                Path(ch, kind, i, path);
                if ((path.back() - palm[i]).Length() > takeReach && (At(path, 0.85f) - palm[i]).Length() > takeReach) continue;
                if (!wardrobe.Wear(g)) continue;   // something's in the way (her outer layers)
                gm.off = 1.f;
                motor.Release(world, i);
                for (int j = 0; j < 2; ++j) if (motor.HeldBody(j) == body) { motor.Release(world, j); s.hand[j] = in.hands[j].grip; }
                world.Despawn(s.entity);
                s.entity = 0;
                s.hand[i] = true;
                break;
            }
        } else {
            // its object went away (the world was reset): it's lying at her feet
            const Vec3 at = ch.BonePos(ch.Rig().foot[0]) + ch.placement.r.Rotate({0.25f, 0.f, 0.15f});
            float gy = at.y;
            RayHit hit;
            if (world.RayCast(at + Vec3{0, 1.f, 0}, {0, -1, 0}, 3.f, hit, true)) gy = hit.point.y;
            s.entity = SpawnObject(world, gm, {at.x, gy + 0.03f, at.z});
        }

        // pushed down to her ankles and let go: when she steps (a foot moves), she steps out of it and it stays on the floor
        if (gm.worn && kind != GarmentKind::Top && gm.off >= 0.75f && !s.hand[0] && !s.hand[1]) {
            const RigMap& r = ch.Rig();
            if (!s.atFeet) { s.atFeet = true; s.feetAt[0] = ch.BonePos(r.foot[0]); s.feetAt[1] = ch.BonePos(r.foot[1]); }
            const float moved = std::max((ch.BonePos(r.foot[0]) - s.feetAt[0]).Length(), (ch.BonePos(r.foot[1]) - s.feetAt[1]).Length());
            if (moved > 0.12f) {
                const Vec3 c = (s.feetAt[0] + s.feetAt[1]) * 0.5f;
                float gy = c.y - 0.08f;
                RayHit hit;
                if (world.RayCast(c + Vec3{0, 0.3f, 0}, {0, -1, 0}, 2.f, hit, true)) gy = hit.point.y;
                gm.worn = false;
                gm.off = 0.f;
                s.atFeet = false;
                if (s.entity) world.Despawn(s.entity);
                s.entity = SpawnObject(world, gm, {c.x, gy + 0.02f, c.z});
                feel_.push_back({"stepped out of something", 1.f});
            }
        } else {
            s.atFeet = false;
        }

        // how it looks: slid along its way (the mesh rides along rigidly)
        if (gm.worn && gm.off > 0.f) {
            Vec3 shift{};
            for (int i = 0; i < 2; ++i) { Path(ch, kind, i, path); shift = shift + (At(path, gm.off) - path.front()) * 0.5f; }
            for (int m : gm.meshes) ch.SetMeshOffset(static_cast<size_t>(m), shift);
        } else {
            for (int m : gm.meshes) ch.SetMeshOffset(static_cast<size_t>(m), {});
        }

        // and how it feels
        if (gm.worn) {
            const char* where = kind == GarmentKind::Bottom ? "hips" : kind == GarmentKind::Top ? "chest" : "feet";
            if (gm.off < 0.1f) feel_.push_back({std::string("something snug on my ") + where, 0.3f});
            else if (kind == GarmentKind::Bottom) feel_.push_back({gm.off < 0.45f ? "cloth bunched at my thighs" : gm.off < 0.75f ? "cloth around my knees" : "cloth around my ankles", 0.7f});
            else feel_.push_back({std::string("cloth hanging off my ") + where, 0.6f});
        }
    }
    for (int i = 0; i < 2; ++i) {
        bool busy = false;
        for (const St& s : st_) busy |= s.hand[i];
        motor.SetHandBusy(i, busy);
        wasGrip_[i] = in.hands[i].grip;
    }
}

} // namespace eon
