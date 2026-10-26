#include "World.h"

#include <algorithm>
#include <cmath>

namespace eon {

namespace {

float P(const nlohmann::json& q, const char* key, float def) {
    return q.contains(key) && q[key].is_number() ? q[key].get<float>() : def;
}

Vec3 ReadAxis(const nlohmann::json& q, const char* key, Vec3 def) {
    if (q.contains(key) && q[key].is_array() && q[key].size() == 3)
        return Vec3{q[key][0].get<float>(), q[key][1].get<float>(), q[key][2].get<float>()}.Normalized(def);
    return def;
}

float ExtentAlong(const Vec3& halfExtent, const Vec3& axis) {
    return std::fabs(axis.x) * halfExtent.x + std::fabs(axis.y) * halfExtent.y + std::fabs(axis.z) * halfExtent.z;
}

Rgb Brighter(Rgb c, float k) {
    auto f = [k](unsigned char v) { return static_cast<unsigned char>(std::min(255.f, v * k + 12.f)); };
    return {f(c.r), f(c.g), f(c.b)};
}

bool IsRope(const Entity& e) { return e.type == "wire" && !e.params.value("hidden", false); }

// Default terminal positions in entity local space.
Vec3 DefaultTerminal(const Entity& e, int term) {
    const Vec3 h = e.size * 0.5f;
    const float s = term == 0 ? -1.f : 1.f;
    if (e.type == "battery") return {s * 0.25f * e.size.x, h.y, 0.f};
    if (e.type == "lamp") return {s * h.x, -0.7f * h.y, 0.f};
    return {s * h.x, 0.f, 0.f};
}

constexpr float kSnapRadius = 0.012f;      // clip latches when released this close to a terminal (m)
constexpr float kTouchRadius = 0.006f;     // terminals this close conduct (m)
constexpr float kSegmentLength = 0.03f;    // rope segment length (m)

} // namespace

World::World() : physics_(std::make_unique<Physics>()) {}
World::~World() = default;

bool World::Load(const std::string& path, std::string* error) {
    Zone z;
    if (!LoadZone(path, z, error)) return false;
    path_ = path;
    SetZone(z);
    return true;
}

void World::SetZone(const Zone& zone) {
    zone_ = zone;
    Reset();
}

void World::Reset() {
    physics_->Clear();
    bodies_.clear();
    terminals_.clear();
    clips_.clear();
    buttons_.clear();
    switches_.clear();
    idealLinks_.clear();
    pushes_.clear();
    grab_ = {};   // its bodies were removed by Clear()

    zone_.BuildCircuit(circuit_);
    circuit_.ClearConnections();

    for (Entity& e : zone_.entities) if (!IsRope(e)) BuildEntity(e);

    for (Entity& e : zone_.entities) {
        if (!IsRope(e)) continue;
        std::vector<std::pair<int, int>> ends;
        for (const TerminalLink& l : zone_.links) {
            if (l.entityA == e.id) { const int t = FindTerminal(l.entityB, l.termB); if (t >= 0) ends.push_back({l.termA, t}); }
            if (l.entityB == e.id) { const int t = FindTerminal(l.entityA, l.termA); if (t >= 0) ends.push_back({l.termB, t}); }
        }
        BuildRope(e, ends);
    }

    for (const TerminalLink& l : zone_.links) {
        const Entity* a = zone_.Find(l.entityA);
        const Entity* b = zone_.Find(l.entityB);
        if (!a || !b || a->part < 0 || b->part < 0 || IsRope(*a) || IsRope(*b)) continue;
        idealLinks_.push_back({a->part, l.termA, b->part, l.termB});
    }

    accumulator_ = 0.0;
    simTime_ = 0.0;
    UpdateSwitchStates();
    UpdateConnections();
    circuit_.Step(0.0);
}

int World::AddBody(const BodyDesc& d, int entity, Role role, const Vec3& drawSize, Rgb color, Quat drawOffset) {
    BodyDesc desc = d;
    desc.userData = bodies_.size() + 1;
    RtBody b;
    b.h = physics_->CreateBody(desc);
    b.entity = entity;
    b.role = role;
    b.size = drawSize;
    b.color = color;
    b.drawOffset = drawOffset;
    bodies_.push_back(b);
    return static_cast<int>(bodies_.size()) - 1;
}

int World::AddTerminal(int body, const Vec3& local, int part, int term, int entity) {
    Terminal t;
    t.body = body;
    t.local = local;
    t.part = part;
    t.term = term;
    t.entity = entity;
    terminals_.push_back(t);
    return static_cast<int>(terminals_.size()) - 1;
}

int World::FindTerminal(int entity, int term) const {
    for (int i = 0; i < (int)terminals_.size(); ++i)
        if (terminals_[i].entity == entity && terminals_[i].term == term) return i;
    return -1;
}

void World::BuildEntity(Entity& e) {
    const Quat rot = Quat::FromEulerDeg(e.rotDeg);
    const Vec3 he = e.size * 0.5f;
    const std::string material = e.params.value("material", std::string());

    BodyDesc d;
    d.pos = e.pos;
    d.rot = rot;
    d.halfExtent = he;
    d.mass = e.massKg;
    d.friction = material == "soft" ? 0.9f : material == "metal" ? 0.4f : 0.6f;
    d.restitution = material == "soft" ? 0.02f : 0.1f;
    d.fastMoving = e.massKg > 0.f && std::min({he.x, he.y, he.z}) < 0.015f;

    const bool isButton = e.type == "button";
    const bool isSwitch = e.type == "switch";
    uint32_t group = 0;
    if (isButton || isSwitch) {
        group = physics_->NewGroup(2);
        physics_->DisableCollision(group, 0, 1);
        d.group = group;
        d.subGroup = 0;
    }

    const int main = AddBody(d, e.id, Role::Main, e.size, e.color);

    if (e.part >= 0) {
        for (int term = 0; term < 2; ++term) {
            Vec3 local = DefaultTerminal(e, term);
            if (e.params.contains("terminals") && e.params["terminals"].is_array() && e.params["terminals"].size() == 2) {
                const auto& t = e.params["terminals"][term];
                local = {t[0].get<float>(), t[1].get<float>(), t[2].get<float>()};
            }
            AddTerminal(main, local, e.part, term, e.id);
        }
    }

    if (isButton) {
        ButtonRt b;
        b.entity = e.id;
        b.base = main;
        b.axisLocal = ReadAxis(e.params, "axis", {0, 1, 0});
        b.travel = P(e.params, "travel", 0.008f);
        b.force = P(e.params, "force", 2.5f);
        const Vec3 p = b.axisLocal;
        const float hp = ExtentAlong(he, p);
        const Vec3 plHalf{std::fabs(p.x) > 0.5f ? 0.006f : 0.6f * he.x,
                          std::fabs(p.y) > 0.5f ? 0.006f : 0.6f * he.y,
                          std::fabs(p.z) > 0.5f ? 0.006f : 0.6f * he.z};
        const Vec3 Pw = rot.Rotate(p);
        const Vec3 c = e.pos + Pw * (hp + 0.006f);

        BodyDesc pd;
        pd.pos = c;
        pd.rot = rot;
        pd.halfExtent = plHalf;
        pd.mass = 0.01f;
        pd.group = group;
        pd.subGroup = 1;
        pd.fastMoving = true;
        b.plunger = AddBody(pd, e.id, Role::Plunger, plHalf * 2.f, Brighter(e.color, 1.15f));
        // Stiff spring capped at `force`: behaves like a preloaded button spring (~force N to press).
        b.slider = physics_->AddSpringSlider(bodies_[main].h, bodies_[b.plunger].h, c, Pw, -b.travel, 0.f, 40.f, 1.0f, b.force);
        buttons_.push_back(b);
    }

    if (isSwitch) {
        SwitchRt s;
        s.entity = e.id;
        s.base = main;
        const Vec3 p = ReadAxis(e.params, "axis", {0, 1, 0});
        Vec3 h = ReadAxis(e.params, "hinge", p.Cross({0, 1, 0}));
        if (h.Length() < 0.1f) h = {0, 0, 1};
        h = (h - p * p.Dot(h)).Normalized({0, 0, 1});
        const Vec3 u = p.Cross(h);
        s.axisLocal = p;
        s.hingeLocal = h;
        s.longLocal = u;
        s.halfLong = 0.35f * ExtentAlong(he, u);
        s.halfP = 0.006f;
        const float halfH = 0.4f * ExtentAlong(he, h);
        s.limit = P(e.params, "throw", 0.3f);
        s.detent = P(e.params, "detent", 0.015f);

        const Vec3 Pw = rot.Rotate(p);
        const Vec3 Hw = rot.Rotate(h);
        const Vec3 pivot = e.pos + Pw * ExtentAlong(he, p);
        const bool closed = e.params.value("closed", false);
        const float a0 = (closed ? 0.9f : -0.9f) * s.limit;
        const Quat q = Quat::AxisAngle(Hw, a0);

        BodyDesc ld;
        ld.pos = pivot + q.Rotate(Pw * s.halfP);
        ld.rot = (q * rot * Quat::FromBasis(u, p, h)).Normalized();
        ld.halfExtent = {s.halfLong, s.halfP, halfH};
        ld.mass = 0.05f;          // rocker + snap mechanism; too light and contact forces spin it wildly
        ld.angularDamping = 2.f;
        ld.group = group;
        ld.subGroup = 1;
        ld.fastMoving = true;
        s.lever = AddBody(ld, e.id, Role::Lever, ld.halfExtent * 2.f, Brighter(e.color, 0.9f));
        s.hinge = physics_->AddHinge(bodies_[main].h, bodies_[s.lever].h, pivot, Hw, Pw, q.Rotate(Pw),
                                     -s.limit, s.limit, 0.0005f);
        switches_.push_back(s);
    }
}

void World::BuildRope(Entity& wire, const std::vector<std::pair<int, int>>& endLinks) {
    float L = P(wire.params, "length", std::max({wire.size.x, wire.size.y, wire.size.z}));
    const float r = P(wire.params, "radius", 0.0025f);
    const float breakForce = P(wire.params, "breakForce", 12.f);
    const Quat rot = Quat::FromEulerDeg(wire.rotDeg);
    const Vec3 X = rot.Rotate({1, 0, 0});

    Vec3 ends[2] = {wire.pos - X * (L * 0.5f), wire.pos + X * (L * 0.5f)};
    int linkTerm[2] = {-1, -1};
    for (const auto& [end, t] : endLinks) {
        if (end < 0 || end > 1) continue;
        ends[end] = TerminalWorld(terminals_[t]);
        linkTerm[end] = t;
    }
    if ((linkTerm[0] >= 0) != (linkTerm[1] >= 0)) {
        const int fixedEnd = linkTerm[0] >= 0 ? 0 : 1;
        const int freeEnd = 1 - fixedEnd;
        const Vec3 dir = ends[freeEnd] - ends[fixedEnd];
        if (dir.Length() > 0.95f * L) ends[freeEnd] = ends[fixedEnd] + dir.Normalized({1, 0, 0}) * (0.95f * L);
    }

    const Vec3 A = ends[0], B = ends[1];
    const float d = Distance(A, B);
    if (d > L) L = d * 1.02f;

    // Polyline: straight if nearly taut, else a sideways bow with roughly the right arc length
    // (bowing sideways keeps the slack out of the table/floor it lies on; gravity settles the rest).
    const float sag = d > 0.98f * L ? 0.f : std::sqrt(std::max(0.f, 3.f * std::max(d, 0.01f) * (L - d) / 8.f));
    Vec3 bow = (B - A).Cross({0, 1, 0});
    bow = bow.Length() > 1e-4f ? bow.Normalized() : Vec3{1, 0, 0};
    std::vector<Vec3> poly(65);
    for (int i = 0; i <= 64; ++i) {
        const float t = i / 64.f;
        poly[i] = A + (B - A) * t + bow * (4.f * sag * t * (1.f - t));
    }
    std::vector<float> cum(poly.size(), 0.f);
    for (size_t i = 1; i < poly.size(); ++i) cum[i] = cum[i - 1] + Distance(poly[i], poly[i - 1]);
    const float S = std::max(cum.back(), 0.01f);
    const int n = std::max(2, static_cast<int>(std::lround(S / kSegmentLength)));

    std::vector<Vec3> pts(n + 1);
    size_t k = 1;
    for (int i = 0; i <= n; ++i) {
        const float target = S * i / n;
        while (k < poly.size() - 1 && cum[k] < target) ++k;
        const float span = std::max(cum[k] - cum[k - 1], 1e-6f);
        const float t = std::clamp((target - cum[k - 1]) / span, 0.f, 1.f);
        pts[i] = poly[k - 1] + (poly[k] - poly[k - 1]) * t;
    }

    const uint32_t group = physics_->NewGroup(n + 2);
    for (int i = 0; i + 1 < n + 2; ++i) physics_->DisableCollision(group, i, i + 1);
    for (int i = 0; i + 2 < n + 2; ++i) physics_->DisableCollision(group, i, i + 2);

    const float segMass = std::max(0.0015f, 0.008f * S / n);
    auto makeClip = [&](const Vec3& at, const Vec3& tangent, uint32_t sub) {
        BodyDesc c;
        c.pos = at;
        c.rot = Quat::FromTo({0, 0, 1}, tangent);
        c.halfExtent = {0.004f, 0.003f, 0.008f};
        c.mass = 0.004f;
        c.friction = 0.7f;
        c.restitution = 0.f;
        c.linearDamping = 0.3f;
        c.angularDamping = 0.8f;
        c.fastMoving = true;
        c.group = group;
        c.subGroup = sub;
        return AddBody(c, wire.id, Role::Clip, c.halfExtent * 2.f, Brighter(wire.color, 1.1f));
    };

    const int clipA = makeClip(pts[0], pts[0] - pts[1], 0);
    std::vector<int> segs(n);
    for (int i = 0; i < n; ++i) {
        const Vec3 dir = pts[i + 1] - pts[i];
        BodyDesc s;
        s.shape = Shape::Capsule;
        s.pos = (pts[i] + pts[i + 1]) * 0.5f;
        s.rot = Quat::FromTo({0, 1, 0}, dir);
        s.radius = r;
        s.halfHeight = std::max(0.0005f, dir.Length() * 0.5f);
        s.mass = segMass;
        s.friction = 0.8f;
        s.restitution = 0.f;
        s.linearDamping = 0.4f;
        s.angularDamping = 0.8f;
        s.fastMoving = true;
        s.group = group;
        s.subGroup = i + 1;
        segs[i] = AddBody(s, wire.id, Role::RopeSegment, {2 * r, dir.Length() + r, 2 * r}, wire.color);
    }
    const int clipB = makeClip(pts[n], pts[n] - pts[n - 1], n + 1);

    physics_->AddPoint(bodies_[clipA].h, bodies_[segs[0]].h, pts[0]);
    for (int i = 1; i < n; ++i) physics_->AddPoint(bodies_[segs[i - 1]].h, bodies_[segs[i]].h, pts[i]);
    physics_->AddPoint(bodies_[segs[n - 1]].h, bodies_[clipB].h, pts[n]);

    const int clipBodies[2] = {clipA, clipB};
    for (int end = 0; end < 2; ++end) {
        const int t = AddTerminal(clipBodies[end], {0, 0, 0}, wire.part, end, wire.id);
        Clip c;
        c.body = clipBodies[end];
        c.terminal = t;
        c.breakForce = breakForce;
        clips_.push_back(c);
        terminals_[t].clip = static_cast<int>(clips_.size()) - 1;
        if (linkTerm[end] >= 0) AttachClip(terminals_[t].clip, linkTerm[end]);
    }
}

Vec3 World::ToWorld(int body, const Vec3& local) const {
    Vec3 p; Quat q;
    physics_->GetTransform(bodies_[body].h, p, q);
    return p + q.Rotate(local);
}

Vec3 World::ToLocal(int body, const Vec3& world) const {
    Vec3 p; Quat q;
    physics_->GetTransform(bodies_[body].h, p, q);
    return q.Conjugate().Rotate(world - p);
}

Vec3 World::TerminalWorld(const Terminal& t) const {
    return t.body >= 0 ? ToWorld(t.body, t.local) : Vec3{};
}

void World::AttachClip(int ci, int ti) {
    Clip& c = clips_[ci];
    if (c.joint != kNoConstraint) DetachClip(ci);
    const Terminal& t = terminals_[ti];
    const BodyHandle other = t.body >= 0 ? bodies_[t.body].h : kNoBody;
    c.joint = physics_->AddFixed(other, bodies_[c.body].h);
    c.attachedTo = ti;
}

void World::DetachClip(int ci) {
    Clip& c = clips_[ci];
    if (c.joint != kNoConstraint) physics_->RemoveConstraint(c.joint);
    c.joint = kNoConstraint;
    c.attachedTo = -1;
    c.cooldown = 0.6f;
}

void World::Tick(double realDt) {
    accumulator_ += std::clamp(realDt, 0.0, 0.25);
    while (accumulator_ >= kFixedDt) {
        FixedStep(static_cast<float>(kFixedDt));
        accumulator_ -= kFixedDt;
        simTime_ += kFixedDt;
    }
}

void World::Advance(double seconds) {
    const int steps = static_cast<int>(std::lround(seconds / kFixedDt));
    for (int i = 0; i < steps; ++i) {
        FixedStep(static_cast<float>(kFixedDt));
        simTime_ += kFixedDt;
    }
}

void World::FixedStep(float dt) {
    if (physicsOn_) {
        ApplyForces(dt);
        physics_->Step(dt);
        UpdateClips(dt);
        if (grab_.joint != kNoConstraint) {
            // Grip slips if the object resists harder than the hand can hold for ~50 ms.
            grab_.overloadSteps = physics_->ConstraintForce(grab_.joint, dt) > grab_.maxForce ? grab_.overloadSteps + 1 : 0;
            if (grab_.overloadSteps > 6) EndGrab();
        }
    }
    UpdateSwitchStates();
    UpdateConnections();
    circuit_.Step(dt);
}

void World::ApplyForces(float dt) {
    if (grab_.body >= 0 && grab_.hand != kNoBody) {
        Vec3 step = grab_.target - grab_.handPos;
        const float maxStep = grab_.maxSpeed * dt;
        if (step.Length() > maxStep) step = step.Normalized() * maxStep;
        grab_.handPos += step;
        physics_->MoveKinematic(grab_.hand, grab_.handPos, Quat{}, dt);
    }

    for (PushRt& p : pushes_) {
        if (p.body < 0) continue;
        physics_->AddForce(bodies_[p.body].h, p.force, ToWorld(p.body, p.local));
        if (p.timeLeft > 0.f) {
            p.timeLeft -= dt;
            if (p.timeLeft <= 0.f) p.body = -1;
        }
    }
    pushes_.erase(std::remove_if(pushes_.begin(), pushes_.end(), [](const PushRt& p) { return p.body < 0; }), pushes_.end());

    // Toggle switch detent: snaps the rocker to whichever side it is on.
    for (const SwitchRt& s : switches_) {
        const float a = physics_->HingeAngle(s.hinge);
        if (std::fabs(a) < 0.01f) continue;
        Vec3 bp; Quat bq;
        physics_->GetTransform(bodies_[s.base].h, bp, bq);
        const Vec3 tau = bq.Rotate(s.hingeLocal) * (a > 0 ? s.detent : -s.detent);
        physics_->AddTorque(bodies_[s.lever].h, tau);
        physics_->AddTorque(bodies_[s.base].h, -tau);
    }
}

void World::UpdateClips(float dt) {
    for (int ci = 0; ci < (int)clips_.size(); ++ci) {
        Clip& c = clips_[ci];
        c.cooldown = std::max(0.f, c.cooldown - dt);

        if (c.attachedTo >= 0) {
            if (simTime_ > 0.5 && physics_->ConstraintForce(c.joint, dt) > c.breakForce) DetachClip(ci);
            continue;
        }
        if (c.cooldown > 0.f || grab_.body == c.body) continue;

        const Vec3 cp = ToWorld(c.body, {0, 0, 0});
        const int ownEntity = terminals_[c.terminal].entity;
        int best = -1;
        float bestD = kSnapRadius;
        for (int ti = 0; ti < (int)terminals_.size(); ++ti) {
            const Terminal& t = terminals_[ti];
            if (t.body < 0 || t.clip >= 0 || t.entity == ownEntity) continue;
            const float dist = Distance(cp, TerminalWorld(t));
            if (dist < bestD) { bestD = dist; best = ti; }
        }
        if (best >= 0) AttachClip(ci, best);
    }
}

void World::UpdateSwitchStates() {
    for (const ButtonRt& b : buttons_) {
        const Entity* e = zone_.Find(b.entity);
        if (!e || e->part < 0) continue;
        circuit_.GetPart(e->part).closed = physics_->SliderPosition(b.slider) < -0.6f * b.travel;
    }
    for (const SwitchRt& s : switches_) {
        const Entity* e = zone_.Find(s.entity);
        if (!e || e->part < 0) continue;
        circuit_.GetPart(e->part).closed = physics_->HingeAngle(s.hinge) > 0.f;
    }
}

void World::UpdateConnections() {
    circuit_.ClearConnections();
    for (const IdealLink& l : idealLinks_) circuit_.Connect({l.partA, l.termA}, {l.partB, l.termB});

    for (const Clip& c : clips_) {
        if (c.attachedTo < 0) continue;
        const Terminal& a = terminals_[c.terminal];
        const Terminal& b = terminals_[c.attachedTo];
        circuit_.Connect({a.part, a.term}, {b.part, b.term});
    }

    // Anything conductive physically touching conducts.
    std::vector<Vec3> pos(terminals_.size());
    for (size_t i = 0; i < terminals_.size(); ++i) pos[i] = TerminalWorld(terminals_[i]);
    for (size_t i = 0; i < terminals_.size(); ++i) {
        if (terminals_[i].body < 0) continue;
        for (size_t j = i + 1; j < terminals_.size(); ++j) {
            if (terminals_[j].body < 0 || terminals_[i].part == terminals_[j].part) continue;
            if (Distance(pos[i], pos[j]) < kTouchRadius)
                circuit_.Connect({terminals_[i].part, terminals_[i].term}, {terminals_[j].part, terminals_[j].term});
        }
    }
}

bool World::RayCast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out) const {
    BodyHandle h;
    Vec3 p;
    float dist;
    if (!physics_->RayCast(origin, dir, maxDist, h, p, dist)) return false;
    const int idx = static_cast<int>(physics_->GetUserData(h)) - 1;
    if (idx < 0 || idx >= (int)bodies_.size()) return false;
    out.body = idx;
    out.entity = bodies_[idx].entity;
    out.point = p;
    out.dist = dist;
    return true;
}

void World::BeginGrab(int body, const Vec3& worldPoint) {
    EndGrab();
    if (body < 0 || body >= (int)bodies_.size() || !physics_->IsDynamic(bodies_[body].h)) return;
    BodyDesc hand;
    hand.pos = worldPoint;
    hand.halfExtent = {0.005f, 0.005f, 0.005f};
    hand.kinematic = true;
    hand.ghost = true;
    grab_.hand = physics_->CreateBody(hand);
    grab_.joint = physics_->AddPoint(grab_.hand, bodies_[body].h, worldPoint);
    grab_.body = body;
    grab_.local = ToLocal(body, worldPoint);
    grab_.target = grab_.handPos = worldPoint;
    physics_->Wake(bodies_[body].h);
}

void World::SetGrabTarget(const Vec3& t) { grab_.target = t; }

void World::EndGrab() {
    if (grab_.joint != kNoConstraint) physics_->RemoveConstraint(grab_.joint);
    if (grab_.hand != kNoBody) physics_->RemoveBody(grab_.hand);
    grab_ = {};
}
Vec3 World::GrabPoint() const { return grab_.body >= 0 ? ToWorld(grab_.body, grab_.local) : Vec3{}; }

void World::SetPush(int body, const Vec3& worldPoint, const Vec3& force, float seconds) {
    if (body < 0 || body >= (int)bodies_.size()) return;
    pushes_.erase(std::remove_if(pushes_.begin(), pushes_.end(), [](const PushRt& p) { return p.tag == 0; }), pushes_.end());
    PushRt p;
    p.body = body;
    p.local = ToLocal(body, worldPoint);
    p.force = force;
    p.timeLeft = seconds;
    pushes_.push_back(p);
    physics_->Wake(bodies_[body].h);
}

void World::ClearPush() {
    pushes_.erase(std::remove_if(pushes_.begin(), pushes_.end(), [](const PushRt& p) { return p.tag == 0; }), pushes_.end());
}

void World::DebugActuate(int entityId, bool on) {
    pushes_.erase(std::remove_if(pushes_.begin(), pushes_.end(), [entityId](const PushRt& p) { return p.tag == entityId; }), pushes_.end());

    for (const ButtonRt& b : buttons_) {
        if (b.entity != entityId || !on) continue;
        Vec3 pp; Quat pq;
        physics_->GetTransform(bodies_[b.plunger].h, pp, pq);
        PushRt p;
        p.body = b.plunger;
        p.local = {0, 0, 0};
        p.force = pq.Rotate(b.axisLocal) * (-2.5f * b.force);
        p.tag = entityId;
        pushes_.push_back(p);
        physics_->Wake(bodies_[b.plunger].h);
    }

    for (const SwitchRt& s : switches_) {
        if (s.entity != entityId) continue;
        Vec3 bp, lp; Quat bq, lq;
        physics_->GetTransform(bodies_[s.base].h, bp, bq);
        physics_->GetTransform(bodies_[s.lever].h, lp, lq);
        const Vec3 Pw = bq.Rotate(s.axisLocal);
        const Vec3 Hw = bq.Rotate(s.hingeLocal);
        const Vec3 U = lq.Rotate({1, 0, 0});
        const float want = on ? 1.f : -1.f;
        // Pick the rocker end whose push (into the plate) turns the lever toward the wanted side.
        const float sideSign = (U.Cross(-Pw)).Dot(Hw) * want > 0 ? 1.f : -1.f;
        PushRt p;
        p.body = s.lever;
        p.local = {sideSign * s.halfLong * 0.9f, 0, 0};
        p.force = -Pw * 3.f;
        p.timeLeft = 0.3f;
        p.tag = entityId;
        pushes_.push_back(p);
        physics_->Wake(bodies_[s.lever].h);
    }
}

const Part* World::PartOf(int entityId) const {
    const Entity* e = zone_.Find(entityId);
    return e && e->part >= 0 ? &circuit_.GetPart(e->part) : nullptr;
}

int World::EntityOfBody(int body) const { return body >= 0 && body < (int)bodies_.size() ? bodies_[body].entity : 0; }
BodyHandle World::HandleOf(int body) const { return body >= 0 && body < (int)bodies_.size() ? bodies_[body].h : kNoBody; }
Vec3 World::BodyPosition(int body) const { return ToWorld(body, {0, 0, 0}); }

int World::MainBodyOf(int entityId) const {
    for (int i = 0; i < (int)bodies_.size(); ++i)
        if (bodies_[i].entity == entityId && bodies_[i].role == Role::Main) return i;
    return -1;
}

int World::ClipAttachedEntity(int wireEntityId, int end) const {
    for (const Clip& c : clips_) {
        const Terminal& own = terminals_[c.terminal];
        if (own.entity == wireEntityId && own.term == end) return c.attachedTo >= 0 ? terminals_[c.attachedTo].entity : 0;
    }
    return 0;
}

int World::ClipBodyOf(int wireEntityId, int end) const {
    for (const Clip& c : clips_) {
        const Terminal& own = terminals_[c.terminal];
        if (own.entity == wireEntityId && own.term == end) return c.body;
    }
    return -1;
}

std::vector<Vec3> World::TerminalPositions(int entityId) const {
    std::vector<Vec3> out;
    for (const Terminal& t : terminals_)
        if (t.entity == entityId && t.body >= 0) out.push_back(TerminalWorld(t));
    return out;
}

void World::BuildRenderFrame(RenderFrame& out) const {
    out.pieces.clear();
    out.lights.clear();
    for (const RtBody& b : bodies_) {
        RenderPiece p;
        physics_->GetTransform(b.h, p.pos, p.rot);
        p.rot = (p.rot * b.drawOffset).Normalized();
        p.size = b.size;
        p.color = b.color;
        p.entity = b.entity;
        if (b.role == Role::Main) {
            const Entity* e = zone_.Find(b.entity);
            if (e && e->type == "lamp" && e->part >= 0) {
                const Part& part = circuit_.GetPart(e->part);
                p.emissive = static_cast<float>(part.brightness) * 2.f;
                if (part.brightness > 0.001 && out.lights.size() < 8) {
                    const float k = static_cast<float>(part.brightness * part.ratedPower) * 0.4f;
                    out.lights.push_back({p.pos - Vec3{0, b.size.y * 0.6f, 0}, Vec3{k, k * 0.92f, k * 0.78f}});
                }
            }
        }
        out.pieces.push_back(p);
    }
}

} // namespace eon
