#include "Perception.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace eon {

uint32_t Perception::Track(int entity) {
    if (entity <= 0) return 0;
    const uint64_t key = (zoneKey_ << 24) ^ uint64_t(entity);   // the same id in another room is another thing
    auto it = trackOf_.find(key);
    if (it != trackOf_.end()) return it->second;
    // opaque, scrambled ids so nothing about the engine's numbering leaks through
    const uint32_t id = (next_++ * 2654435761u) % 999983u + 17u;
    trackOf_[key] = id;
    return id;
}

void Perception::Sense(const World& world, const Motor& motor, const Character& ch, const BodyState* body, double t, Percept& out) {
    out = Percept{};
    out.t = t;
    zoneKey_ = std::hash<std::string>{}(world.GetZone().name) & 0xffffffffffull;
    const RigMap& r = ch.Rig();
    out.eye = r.head >= 0 ? ch.BonePos(r.head) + Vec3{0, 0.07f, 0} : motor.Position() + Vec3{0, 1.6f, 0};
    out.self = motor.Position();
    out.yaw = motor.Yaw();
    out.stance = motor.CurrentStance();
    out.bumped = motor.Feedback().bumped;
    out.atEdge = motor.Feedback().atEdge;

    // what is in front of her eyes (merge every piece of a thing; check nothing solid is in the way)
    world.BuildRenderFrame(frame_);
    struct Acc { Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f}; Rgb color; float glow = 0.f; float vol = 0.f; };
    std::unordered_map<int, Acc> acc;
    for (const RenderPiece& p : frame_.pieces) {
        if (p.entity <= 0) continue;
        Acc& a = acc[p.entity];
        const Vec3 h = p.size * 0.5f;
        // rotated box -> world bounds
        Vec3 ext{0, 0, 0};
        const Vec3 ax[3] = {p.rot.Rotate({h.x, 0, 0}), p.rot.Rotate({0, h.y, 0}), p.rot.Rotate({0, 0, h.z})};
        for (const Vec3& v : ax) ext = ext + Vec3{std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)};
        a.lo = {std::min(a.lo.x, p.pos.x - ext.x), std::min(a.lo.y, p.pos.y - ext.y), std::min(a.lo.z, p.pos.z - ext.z)};
        a.hi = {std::max(a.hi.x, p.pos.x + ext.x), std::max(a.hi.y, p.pos.y + ext.y), std::max(a.hi.z, p.pos.z + ext.z)};
        const float vol = p.size.x * p.size.y * p.size.z;
        if (vol > a.vol) { a.vol = vol; a.color = p.color; }   // the biggest part's color is what you notice
        a.glow = std::max(a.glow, p.emissive);
    }
    const Vec3 fwd{std::sin(out.yaw), 0, std::cos(out.yaw)};
    const bool lying = motor.Lying();
    const float cosFov = std::cos((lying ? 170.f : fovDeg) * 0.5f * 3.14159265f / 180.f);
    float lightSum = 0.f;
    for (auto& [entity, a] : acc) {
        const Vec3 c = (a.lo + a.hi) * 0.5f;
        const Vec3 size = a.hi - a.lo;
        const bool wide = std::max({size.x, size.y, size.z}) > 2.f;
        // aim at the nearest point of its box (a wall's center may be behind her, its surface isn't)
        Vec3 aim{std::clamp(out.eye.x, a.lo.x, a.hi.x), std::clamp(out.eye.y, a.lo.y, a.hi.y), std::clamp(out.eye.z, a.lo.z, a.hi.z)};
        if (!wide) aim = c;
        Vec3 d = aim - out.eye;
        const float dist = d.Length();
        if (dist < 1e-3f || dist > range) continue;
        d = d * (1.f / dist);
        if (!wide && Vec3{d.x, 0, d.z}.Normalized(fwd).Dot(fwd) < cosFov && d.y > -0.9f) continue;
        // visible if any of a few points on it can be seen (things are often partly covered)
        bool visible = false;
        const Vec3 hs = size * 0.35f;
        const Vec3 pts[9] = {aim, c + Vec3{hs.x, hs.y, hs.z}, c + Vec3{-hs.x, hs.y, hs.z}, c + Vec3{hs.x, hs.y, -hs.z}, c + Vec3{-hs.x, hs.y, -hs.z},
                             c + Vec3{hs.x, -hs.y, hs.z}, c + Vec3{-hs.x, -hs.y, hs.z}, c + Vec3{hs.x, -hs.y, -hs.z}, c + Vec3{-hs.x, -hs.y, -hs.z}};
        for (int k = 0; k < (wide ? 1 : 9) && !visible; ++k) {
            Vec3 dk = pts[k] - out.eye;
            const float lk = dk.Length();
            if (lk < 1e-3f) continue;
            dk = dk * (1.f / lk);
            RayHit hit;
            visible = !world.RayCast(out.eye, dk, lk + 0.05f, hit, true) || hit.entity == entity || hit.dist >= lk - 0.02f;
        }
        if (!visible) continue;
        SeenThing s;
        s.track = Track(entity);
        s.pos = c;
        s.lo = a.lo;
        s.hi = a.hi;
        s.color = a.color;
        s.glow = a.glow;
        s.dist = dist;
        s.wide = wide;
        out.seen.push_back(s);
        lightSum += a.glow;
    }
    std::sort(out.seen.begin(), out.seen.end(), [](const SeenThing& a, const SeenThing& b) { return a.dist < b.dist; });
    out.light = std::min(1.f, lightSum * 0.5f + 0.05f);

    // hands: what's under the palm / fingertip, what she holds and how heavy it feels
    for (int i = 0; i < 2; ++i) {
        HandFeel& h = out.hands[i];
        const MotorFeedback& fb = motor.Feedback();
        h.holding = fb.holding[i];
        h.weight = fb.heldMass[i];
        h.force = fb.handForce[i];
        h.blocked = fb.handBlocked[i];
        h.reaching = motor.Reaching(i);
        if (h.holding) h.held = Track(world.EntityOfBody(motor.HeldBody(i)));
        float bestD = 1e9f;
        int bestE = 0;
        const int probes[2] = {motor.TipBody(i), motor.HandBody(i)};
        const float reachR[2] = {0.03f, 0.065f};
        for (int k = 0; k < 2; ++k) {
            if (probes[k] < 0) continue;
            const Vec3 o = world.BodyPosition(probes[k]);
            const Vec3 dirs[7] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, fwd};
            for (const Vec3& dd : dirs) {
                RayHit hit;
                if (world.RayCast(o, dd, reachR[k], hit, true) && hit.dist < bestD && hit.entity > 0) { bestD = hit.dist; bestE = hit.entity; if (k == 0) h.pointing = true; }
            }
        }
        h.touching = Track(bestE);
        if (h.holding && !h.touching) h.touching = h.held;
    }

    // body: only how it feels, in her words
    if (body) {
        auto feel = [&](const char* w, float v) { if (v > 0.05f) out.feelings.push_back({w, std::min(1.f, v)}); };
        feel("cold", body->shiver * 1.5f + (body->thermal == Thermal::Cold ? 0.4f : body->thermal == Thermal::Freezing ? 0.9f : 0.f));
        feel("hot", body->sweat + (body->thermal == Thermal::Hot ? 0.6f : body->thermal == Thermal::Warm ? 0.25f : 0.f));
        feel("hungry", (body->Get(Need::Hunger) - 40.f) / 50.f);
        feel("thirsty", (body->Get(Need::Thirst) - 40.f) / 50.f);
        feel("tired", (body->Get(Need::Fatigue) - 45.f) / 45.f);
        feel("need to pee", (body->Get(Need::Bladder) - 55.f) / 40.f);
        feel("bored", (body->Get(Need::Boredom) - 45.f) / 45.f);
    }
}

} // namespace eon
