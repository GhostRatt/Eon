// Matter: the physical stuff her needs depend on. Liquids come out of taps (opened by a real valve: a lever,
// switch or button in the zone) and fall as streams under gravity; they fill whatever open container is under
// them, splash on her skin, pour out of tilted cups, pool on the floor and slowly evaporate. Food gets bitten
// down to nothing. Heaters are resistive loads in the circuit that warm the air around them.
#include "World.h"

#include <algorithm>
#include <cmath>

namespace eon {

namespace {
constexpr float kG = 9.81f;
constexpr int kNL = static_cast<int>(Liquid::Count);
float P(const nlohmann::json& j, const char* k, float d) { return j.contains(k) && j[k].is_number() ? j[k].get<float>() : d; }
Vec3 V(const nlohmann::json& j, const char* k, Vec3 d) {
    if (!j.contains(k) || !j[k].is_array() || j[k].size() != 3) return d;
    return {j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()};
}
float SegDist(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float l2 = ab.Dot(ab);
    const float t = l2 > 1e-10f ? std::clamp((p - a).Dot(ab) / l2, 0.f, 1.f) : 0.f;
    return Distance(p, a + ab * t);
}
float Total(const float* ml) { float t = 0.f; for (int i = 0; i < kNL; ++i) t += ml[i]; return t; }
}

const char* LiquidName(Liquid l) { return l == Liquid::Water ? "water" : l == Liquid::Urine ? "urine" : "?"; }

void World::BuildMatter(Entity& e, int main) {
    const nlohmann::json& q = e.params;
    const Vec3 he = e.size * 0.5f;
    if (e.type == "tap") {
        TapRt t;
        t.entity = e.id;
        t.body = main;
        t.outLocal = V(q, "out", Vec3{0, -he.y, 0});
        t.dirLocal = V(q, "dir", Vec3{0, -1, 0}).Normalized({0, -1, 0});
        t.rate = P(q, "rate", 60.f);       // ml/s (a kitchen tap ~100, a drinking fountain ~40)
        t.speed = P(q, "speed", 0.5f);     // m/s leaving the spout
        t.valve = q.value("valve", 0);     // entity whose switch state opens it (lever, switch, button)
        t.powered = q.value("powered", false);
        t.jets = std::max(1, q.value("jets", 1));        // a shower head sprays several jets
        t.spread = P(q, "spread", 0.f);                    // m/s sideways at the outer jets
        taps_.push_back(t);
    } else if (e.type == "container" || e.type == "bowl") {
        ContainerRt c;
        c.entity = e.id;
        c.body = main;
        c.capacity = P(q, "capacity", 300.f);
        c.ml[int(Liquid::Water)] = std::min(c.capacity, P(q, "water", 0.f));
        c.half = he;
        c.drain = q.value("drain", 0);     // e.g. the toilet's flush lever
        containers_.push_back(c);
    } else if (e.type == "food") {
        FoodRt f;
        f.entity = e.id;
        f.body = main;
        f.grams = f.left = P(q, "grams", std::max(1.f, e.massKg * 1000.f));
        f.kcalPerG = P(q, "kcalPerG", 0.52f);    // apple-ish
        f.waterPerG = P(q, "waterPerG", 0.85f);
        f.size = e.size;
        foods_.push_back(f);
    } else if (e.type == "heater") {
        heaters_.push_back({e.id, main});
    }
}

bool World::ValveOpen(int valveEntity) const {
    if (valveEntity <= 0) return true;
    const Part* p = PartOf(valveEntity);
    return p && p->closed;
}

float World::AirTemperature(const Vec3& p) const {
    float t = zone_.ambientC;
    for (const HeaterRt& h : heaters_) {
        const Part* part = PartOf(h.entity);
        if (!part || part->power <= 0.01) continue;
        const float d2 = (BodyPosition(h.body) - p).Dot(BodyPosition(h.body) - p);
        // warm air around it: ~10 °C at 1 m from 1 kW, much more right next to it
        t += std::min(25.f, static_cast<float>(part->power) * 0.012f / (0.25f + d2));
    }
    return t;
}

void World::SetBodyStream(const Vec3& origin, const Vec3& vel, float mlPerSec, Liquid l) {
    bodyStream_.origin = origin;
    bodyStream_.vel = vel;
    bodyStream_.rate = mlPerSec;
    bodyStream_.kind = l;
    bodyStream_.fromContainer = -1;
    bodyStream_.fromBody = true;   // starts inside her own colliders: passes through them
}

Intake World::TakeIntake() {
    Intake i = intake_;
    intake_ = {};
    return i;
}

void World::AddToPuddle(const Vec3& p, Liquid l, float ml) {
    if (ml <= 0.f) return;
    for (PuddleRt& q : puddles_) {
        const float r = std::sqrt(Total(q.ml) / 0.15f / 3.14159f) * 0.01f;   // 1.5 mm deep
        if (Distance(Vec3{q.pos.x, p.y, q.pos.z}, p) < r + 0.08f && std::fabs(q.pos.y - p.y) < 0.05f) {
            const float tot = Total(q.ml);
            q.pos = (q.pos * tot + p * ml) * (1.f / (tot + ml));   // the puddle creeps toward new water
            q.ml[int(l)] += ml;
            return;
        }
    }
    PuddleRt q;
    q.pos = p;
    q.ml[int(l)] = ml;
    q.id = nextPuddleId_++;
    puddles_.push_back(q);
}

void World::FlowStream(const StreamRt& s, float dt, StreamDraw* draw) {
    const float amount = s.rate * dt;
    Vec3 p = s.origin, v = s.vel;
    if (draw) draw->pts.push_back(p);
    for (int k = 0; k < 40; ++k) {
        const float h = 0.02f;
        const Vec3 next = p + v * h + Vec3{0, -0.5f * kG * h * h, 0};
        v.y -= kG * h;
        // an open mouth in the way drinks it
        if (hasMouth_ && mouthOpen_ && SegDist(mouth_, p, next) < 0.035f) {
            intake_.ml[int(s.kind)] += amount;
            if (draw) draw->pts.push_back(mouth_);
            return;
        }
        const Vec3 seg = next - p;
        const float len = seg.Length();
        RayHit hit;
        if (len > 1e-5f && RayCast(p, seg * (1.f / len), len, hit, s.fromBody)) {
            if (draw) draw->pts.push_back(hit.point);
            const RtBody& b = bodies_[hit.body];
            if (b.role == Role::Agent) {
                // on her: skin gets wet, some runs off to the floor below
                intake_.skinMl += amount * 0.7f;
                if (s.kind == Liquid::Urine) intake_.skinUrineMl += amount * 0.7f;
                RayHit g;
                if (RayCast(hit.point + Vec3{0, -0.05f, 0}, {0, -1, 0}, 3.f, g, true)) AddToPuddle(g.point, s.kind, amount * 0.3f);
                return;
            }
            for (ContainerRt& c : containers_) {
                if (c.body != hit.body) continue;
                Vec3 cp; Quat cq;
                physics_->GetTransform(b.h, cp, cq);
                const Vec3 up = cq.Rotate({0, 1, 0});
                const Vec3 local = cq.Conjugate().Rotate(hit.point - cp);
                if (up.y > 0.6f && local.y > c.half.y - 0.012f && c.entity != (s.fromContainer >= 0 ? containers_[s.fromContainer].entity : -1)) {
                    const float room = std::max(0.f, c.capacity - Total(c.ml));
                    const float in = std::min(room, amount);
                    c.ml[int(s.kind)] += in;
                    if (amount - in > 0.f) {   // overflow runs down the side
                        RayHit g;
                        if (RayCast(hit.point + Vec3{c.half.x + 0.02f, 0, 0}, {0, -1, 0}, 3.f, g, true)) AddToPuddle(g.point, s.kind, amount - in);
                    }
                    return;
                }
            }
            // anything else: it lands, runs off, and pools on the first flat surface below
            const Vec3 n = SurfaceNormal(hit.body, hit.point);
            if (n.y > 0.7f && BodyMass(hit.body) <= 0.f) { AddToPuddle(hit.point, s.kind, amount); return; }
            RayHit g;
            if (RayCast(hit.point + n * 0.02f + Vec3{0, -0.02f, 0}, {0, -1, 0}, 3.f, g, true)) AddToPuddle(g.point, s.kind, amount);
            return;
        }
        p = next;
        if (draw) draw->pts.push_back(p);
    }
}

void World::StepMatter(float dt) {
    streamDraw_.clear();
    std::vector<StreamRt> streams;
    for (const TapRt& t : taps_) {
        bool open = ValveOpen(t.valve);
        if (t.powered) { const Part* pp = PartOf(t.entity); open = open && pp && pp->power > 0.5; }
        if (!open) continue;
        Vec3 tp; Quat tq;
        physics_->GetTransform(bodies_[t.body].h, tp, tq);
        const Vec3 dir = tq.Rotate(t.dirLocal);
        const Vec3 o = tp + tq.Rotate(t.outLocal) + dir * 0.006f;   // just clear of the spout
        if (t.jets <= 1) { streams.push_back({o, dir * t.speed, t.rate, t.kind, -1}); continue; }
        const Vec3 side = dir.Cross(std::fabs(dir.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}).Normalized(), side2 = dir.Cross(side);
        for (int j = 0; j < t.jets; ++j) {
            const float a = 6.2831853f * j / (t.jets - 1), r = j == 0 ? 0.f : 1.f;   // one in the middle, the rest in a ring
            const Vec3 lat = (side * std::cos(a) + side2 * std::sin(a)) * (t.spread * r);
            streams.push_back({o + lat * 0.08f, dir * t.speed + lat, t.rate / t.jets, t.kind, -1});
        }
    }
    // tilted containers pour from their lowest rim corner
    for (size_t ci = 0; ci < containers_.size(); ++ci) {
        ContainerRt& c = containers_[ci];
        const float tot = Total(c.ml);
        if (tot < 0.5f) continue;
        Vec3 cp; Quat cq;
        physics_->GetTransform(bodies_[c.body].h, cp, cq);
        const Vec3 up = cq.Rotate({0, 1, 0});
        const float tilt = std::acos(std::clamp(up.y, -1.f, 1.f));
        const float fill = std::clamp(tot / c.capacity, 0.f, 1.f);
        const float spillAt = (1.f - fill) * 1.5708f;   // a full cup spills at the slightest tilt, an empty one never
        if (tilt <= spillAt + 0.05f) continue;
        const float rate = std::min(tot / dt, 600.f * (tilt - spillAt));
        Vec3 rim{0, 0, 0};
        float lo = 1e9f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                const Vec3 w = cp + cq.Rotate(Vec3{sx * c.half.x, c.half.y, sz * c.half.z});
                if (w.y < lo) { lo = w.y; rim = w; }
            }
        Vec3 out = rim - cp;
        out.y = 0;
        Liquid kind = c.ml[int(Liquid::Urine)] > c.ml[int(Liquid::Water)] ? Liquid::Urine : Liquid::Water;
        const float take = rate * dt;
        for (int i = 0; i < kNL; ++i) c.ml[i] = std::max(0.f, c.ml[i] - take * c.ml[i] / tot);
        streams.push_back({rim + out.Normalized() * 0.005f, out.Normalized() * 0.25f, rate, kind, static_cast<int>(ci)});
    }
    if (bodyStream_.rate > 0.f) streams.push_back(bodyStream_);
    for (const StreamRt& s : streams) {
        StreamDraw d;
        d.kind = s.kind;
        FlowStream(s, dt, &d);
        if (d.pts.size() >= 2) streamDraw_.push_back(std::move(d));
    }
    // drains (a flushed toilet, a pulled plug)
    for (ContainerRt& c : containers_)
        if (c.drain > 0 && ValveOpen(c.drain))
            for (float& m : c.ml) m = std::max(0.f, m - 400.f * dt);
    // puddles evaporate (faster when there's more surface)
    for (PuddleRt& q : puddles_) {
        const float tot = Total(q.ml);
        const float loss = (0.01f + 0.0004f * tot) * dt * std::max(0.3f, (AirTemperature(q.pos) - 5.f) / 17.f);
        for (float& m : q.ml) m = std::max(0.f, m - loss * (tot > 0 ? m / tot : 0.f));
    }
    puddles_.erase(std::remove_if(puddles_.begin(), puddles_.end(), [](const PuddleRt& q) { return Total(q.ml) < 0.05f; }), puddles_.end());
    // eaten food is gone from the world
    for (FoodRt& f : foods_) if (f.gone) physics_->SetTransform(bodies_[f.body].h, Vec3{1000.f + f.entity, -500.f, 0.f}, Quat{});
}

float World::Bite(const Vec3& mouth, float reach) {
    FoodRt* best = nullptr;
    float bd = 1e9f;
    for (FoodRt& f : foods_) {
        if (f.gone) continue;
        const float d = Distance(BodyPosition(f.body), mouth) - 0.5f * std::max({f.size.x, f.size.y, f.size.z});
        if (d < reach && d < bd) { bd = d; best = &f; }
    }
    if (!best) return 0.f;
    const float g = std::min(best->left, 12.f);   // a bite
    best->left -= g;
    intake_.foodG += g;
    intake_.kcal += g * best->kcalPerG;
    intake_.ml[int(Liquid::Water)] += g * best->waterPerG;
    if (best->left < 2.f) best->gone = true;
    return g;
}

float World::ContainerMl(int entityId, Liquid l) const {
    for (const ContainerRt& c : containers_)
        if (c.entity == entityId) return l == Liquid::Count ? Total(c.ml) : c.ml[int(l)];
    return 0.f;
}

float World::PuddleMl(Liquid l) const {
    float t = 0.f;
    for (const PuddleRt& q : puddles_) t += l == Liquid::Count ? Total(q.ml) : q.ml[int(l)];
    return t;
}

int World::PuddleCount() const { return static_cast<int>(puddles_.size()); }

float World::FoodLeftG(int entityId) const {
    for (const FoodRt& f : foods_) if (f.entity == entityId) return f.gone ? 0.f : f.left;
    return 0.f;
}

void World::AppendMatterPieces(RenderFrame& out) const {
    const Rgb water{120, 165, 215}, urine{220, 200, 95};
    // food shrinks as it's eaten; eaten food is gone
    for (const FoodRt& f : foods_) {
        const float k = std::cbrt(std::max(0.f, f.left / f.grams));
        for (size_t i = 0; i < out.pieces.size();) {
            if (out.pieces[i].entity == f.entity) {
                if (f.gone) { out.pieces.erase(out.pieces.begin() + i); continue; }
                out.pieces[i].size = out.pieces[i].size * k;
            }
            ++i;
        }
    }
    // heaters glow when they draw power
    for (const HeaterRt& h : heaters_) {
        const Part* part = PartOf(h.entity);
        if (!part) continue;
        const float k = std::clamp(static_cast<float>(part->power / std::max(1.0, part->ratedPower)), 0.f, 1.f);
        for (RenderPiece& rp : out.pieces)
            if (rp.entity == h.entity) {
                // the element glows orange-red as it heats
                rp.color = {static_cast<unsigned char>(rp.color.r + (255 - rp.color.r) * k), static_cast<unsigned char>(rp.color.g + (95 - rp.color.g) * k),
                            static_cast<unsigned char>(rp.color.b + (35 - rp.color.b) * k)};
                rp.emissive = 0.9f * k;
            }
    }
    // liquid surface inside containers (seen over the rim)
    for (const ContainerRt& c : containers_) {
        const float tot = Total(c.ml);
        if (tot < 1.f) continue;
        Vec3 cp; Quat cq;
        physics_->GetTransform(bodies_[c.body].h, cp, cq);
        RenderPiece rp;
        rp.pos = cp + cq.Rotate(Vec3{0, c.half.y + 0.0015f, 0});
        rp.rot = cq;
        const float f = std::clamp(tot / c.capacity, 0.15f, 1.f);
        rp.size = {c.half.x * 1.7f * f, 0.002f, c.half.z * 1.7f * f};
        rp.color = c.ml[int(Liquid::Urine)] > c.ml[int(Liquid::Water)] ? urine : water;
        rp.entity = c.entity;
        out.pieces.push_back(rp);
    }
    // puddles: flat discs (drawn as thin squares) sized by volume
    for (const PuddleRt& q : puddles_) {
        const float tot = Total(q.ml);
        const float r = std::min(0.8f, std::sqrt(tot / 0.15f / 3.14159f) * 0.01f);
        RenderPiece rp;
        rp.pos = q.pos + Vec3{0, 0.0015f, 0};
        rp.size = {r * 1.8f, 0.002f, r * 1.8f};
        rp.color = q.ml[int(Liquid::Urine)] > q.ml[int(Liquid::Water)] ? urine : water;
        rp.entity = 1000000 + q.id;   // a thing she can see (and step in)
        out.pieces.push_back(rp);
    }
    // streams: short segments along the falling path
    int si = 0;
    for (const StreamDraw& d : streamDraw_) {
        ++si;
        for (size_t i = 1; i < d.pts.size(); ++i) {
            const Vec3 a = d.pts[i - 1], b = d.pts[i];
            const Vec3 ab = b - a;
            const float len = ab.Length();
            if (len < 1e-4f) continue;
            RenderPiece rp;
            rp.pos = (a + b) * 0.5f;
            rp.rot = Quat::FromTo({0, 1, 0}, ab);
            rp.size = {0.007f, len, 0.007f};
            rp.color = d.kind == Liquid::Urine ? urine : water;
            rp.entity = 2000000 + si;
            out.pieces.push_back(rp);
        }
    }
}

} // namespace eon
