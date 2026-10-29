#include "World.h"

#include <algorithm>
#include <functional>
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
World::~World() { StopScripts(); }

bool World::Load(const std::string& path, std::string* error) {
    Zone z;
    if (!LoadZone(path, z, error)) return false;
    path_ = path;
    // content root = the folder above zones/ (scripts and objects live beside it)
    {
        std::string dir = path.substr(0, path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\"));
        if (dir.size() >= 6 && dir.substr(dir.size() - 5) == "zones") dir = dir.substr(0, dir.size() - 6);
        if (contentRoot_.empty()) contentRoot_ = dir.empty() ? "." : dir;
    }
    SetZone(z);
    return true;
}

void World::SetZone(const Zone& zone) {
    zone_ = zone;
    std::string log;
    if (!ExpandPrefabs(zone_, contentRoot_.empty() ? "content" : contentRoot_, &log)) ScriptLogLine("objects: " + log);
    Reset();
}

void World::Reset() {
    StopScripts();
    // things that appeared while running (vended apples, spawned objects) go; the layout comes back as built
    {
        std::vector<int> gone;
        for (const Entity& e : zone_.entities) if (e.params.value("spawned", false)) gone.push_back(e.id);
        if (!gone.empty()) {
            auto isGone = [&](int id) { return std::find(gone.begin(), gone.end(), id) != gone.end(); };
            zone_.entities.erase(std::remove_if(zone_.entities.begin(), zone_.entities.end(), [&](const Entity& e) { return isGone(e.id); }), zone_.entities.end());
            zone_.links.erase(std::remove_if(zone_.links.begin(), zone_.links.end(), [&](const TerminalLink& l) { return isGone(l.entityA) || isGone(l.entityB); }), zone_.links.end());
        }
    }
    physics_->Clear();
    bodies_.clear();
    mainBody_.clear();
    terminals_.clear();
    clips_.clear();
    buttons_.clear();
    switches_.clear();
    idealLinks_.clear();
    pushes_.clear();
    drives_.clear();
    kinematic_.clear();
    ++generation_;
    joints_.clear();
    welds_.clear();
    grab_ = {};   // its bodies were removed by Clear()
    doors_.clear(); colds_.clear(); terrains_.clear(); waters_.clear(); plants_.clear(); fruits_.clear(); seeds_.clear(); plantClock_ = 0.0;
    taps_.clear(); containers_.clear(); puddles_.clear(); foods_.clear(); heaters_.clear(); streamDraw_.clear();
    bodyStream_ = {}; intake_ = {}; hasMouth_ = mouthOpen_ = false;

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
    BuildWelds();

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
    StartScripts();
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
    if (role == Role::Main) mainBody_.emplace(entity, static_cast<int>(bodies_.size()) - 1);   // first main body wins
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
    if (e.type == "prefab") return;   // placeholder: its parts are separate entities
    if (BuildOutdoor(e)) return;
    if (BuildPlant(e)) return;
    if (e.params.value("onGround", false)) {   // outdoors: sits on the terrain wherever it's put
        float g;
        if (GroundHeight(e.pos.x, e.pos.z, g)) e.pos.y = g + 0.5f * e.size.y + 0.002f;
    }
    // hidden wires run inside walls: electrical only (ideal links), nothing physical or visible
    if (e.type == "wire" && e.params.value("hidden", false)) return;
    if (e.type == "start") return;   // where Jenny starts in a level: the sim reads it, nothing in the world
    if (e.params.value("ghost", false)) { BuildDoor(e, -1); return; }   // markers and air volumes: no body, not drawn
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
    const bool isLever = e.type == "lever";      // a handle sticking out of a wall plate, thrown up/down
    const bool isSwitch = e.type == "switch" || isLever;
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
        s.halfLong = 0.45f * ExtentAlong(he, u);   // rocker paddle nearly as long as the plate
        s.halfP = 0.006f;
        const float halfH = 0.4f * ExtentAlong(he, h);
        s.limit = P(e.params, "throw", 0.3f);
        s.detent = P(e.params, "detent", 0.015f);

        const Vec3 Pw = rot.Rotate(p);
        const Vec3 Hw = rot.Rotate(h);
        const Vec3 pivot = e.pos + Pw * ExtentAlong(he, p);
        const bool closed = e.params.value("closed", false);
        if (isLever) { s.limit = P(e.params, "throw", 0.6f); s.detent = P(e.params, "detent", 1.0f); }
        const float a0 = (closed ? 0.9f : -0.9f) * s.limit;
        const Quat q = Quat::AxisAngle(Hw, a0);

        BodyDesc ld;
        const float handleHalf = 0.5f * P(e.params, "handle", 0.3f);
        ld.pos = pivot + q.Rotate(Pw * (isLever ? handleHalf : s.halfP));
        ld.rot = (q * rot * Quat::FromBasis(u, p, h)).Normalized();
        ld.halfExtent = isLever ? Vec3{0.016f, handleHalf, 0.016f} : Vec3{s.halfLong, s.halfP, halfH};
        s.isLever = isLever;
        s.handleHalf = handleHalf;
        s.pivot = pivot;
        ld.mass = P(e.params, "leverMass", isLever ? 0.4f : 0.05f);   // rocker + snap mechanism; too light and contact forces spin it wildly
        ld.angularDamping = 2.f;
        ld.group = group;
        ld.subGroup = 1;
        ld.fastMoving = true;
        s.lever = AddBody(ld, e.id, Role::Lever, ld.halfExtent * 2.f, Brighter(e.color, 0.9f));
        s.hinge = physics_->AddHinge(bodies_[main].h, bodies_[s.lever].h, pivot, Hw, Pw, q.Rotate(Pw),
                                     -s.limit, s.limit, 0.0005f);
        switches_.push_back(s);
    }
    BuildMatter(e, main);
    BuildDoor(e, main);
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
    lastDt_ = dt;
    if (physicsOn_) {
        for (const KinRt& k : kinematic_)
            if (k.body >= 0 && k.body < (int)bodies_.size()) physics_->MoveKinematic(bodies_[k.body].h, k.pos, k.rot, dt);
        for (const DriveRt& d : drives_) {
            if (d.body < 0 || d.body >= (int)bodies_.size()) continue;
            const BodyHandle h = bodies_[d.body].h;
            const float m = physics_->GetMass(h);
            if (m <= 0.f) continue;
            const float w = 2.f * 3.14159265f * d.freq;
            const Vec3 p = physics_->GetPosition(h);
            Vec3 f = (d.target - p) * (m * w * w) - physics_->GetLinearVelocity(h) * (2.f * m * w * d.damping);
            const float len = f.Length();
            if (len > d.maxForce) f = f * (d.maxForce / len);
            physics_->AddForce(h, f, p);
        }
        StepDoors(dt);
        StepWater(dt);
        StepPlants(dt);
        ApplyForces(dt);
        physics_->Step(dt);
        UpdateClips(dt);
        StepWelds(dt);
        if (grab_.joint != kNoConstraint) {
            // Grip slips if the object resists harder than the hand can hold for ~50 ms.
            grab_.overloadSteps = physics_->ConstraintForce(grab_.joint, dt) > grab_.maxForce ? grab_.overloadSteps + 1 : 0;
            if (grab_.overloadSteps > 6) EndGrab();
        }
    }
    UpdateSwitchStates();
    UpdateConnections();
    circuit_.Step(dt);
    StepMatter(dt);
    StepScripts(dt);
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

    for (const SteadyRt& st : steady_) {
        if (st.body < 0 || st.body >= (int)bodies_.size()) continue;
        const BodyHandle h = bodies_[st.body].h;
        if (!physics_->IsDynamic(h)) continue;
        Vec3 p; Quat q;
        physics_->GetTransform(h, p, q);
        Quat d = (st.rot * q.Conjugate()).Normalized();
        Vec3 axis; float ang;
        d.ToAxisAngle(axis, ang);
        if (ang > 3.14159265f) ang -= 6.2831853f;
        const float I = physics_->GetMass(h) * 0.04f;   // rough box inertia (r ~ 0.2 m)
        const float w = 10.f;
        Vec3 tau = (axis * (ang * w * w) - physics_->GetAngularVelocity(h) * (2.f * w)) * I;
        if (tau.Length() > st.maxTorque) tau = tau.Normalized() * st.maxTorque;
        physics_->AddTorque(h, tau);
        physics_->Wake(h);
    }

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

bool World::RayCast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out, bool skipAgent) const {
    BodyHandle h;
    Vec3 p;
    float dist;
    if (!physics_->RayCast(origin, dir, maxDist, h, p, dist, kNoBody, skipAgent)) return false;
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

    for (const DoorRt& d : doors_) {
        // pull it open (on) or push it shut (off) by its free edge, like a hand would
        const Entity* e = zone_.Find(entityId);
        if (d.entity != entityId || !e) continue;
        const bool left = e->params.value("hingeSide", std::string("left")) == "left";
        Vec3 dp; Quat dq;
        physics_->GetTransform(bodies_[d.body].h, dp, dq);
        PushRt p;
        p.body = d.body;
        p.local = {(left ? 0.4f : -0.4f) * e->size.x, 0.f, 0.5f * e->size.z};
        p.force = dq.Rotate(Vec3{0, 0, on ? 1.f : -1.f}) * (3.f * d.seal / (0.9f * e->size.x) + 2.f * e->massKg);
        p.timeLeft = on ? 0.5f : 1.5f;
        p.tag = entityId;
        pushes_.push_back(p);
        physics_->Wake(bodies_[d.body].h);
    }

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
        if (s.isLever) {
            // a lever: push its tip around the hinge, the way a hand throws it
            const Vec3 tip = lp + lq.Rotate(Vec3{0, s.handleHalf * 0.9f, 0});
            PushRt p;
            p.body = s.lever;
            p.local = {0, s.handleHalf * 0.9f, 0};
            p.force = (Hw * want).Cross(tip - s.pivot).Normalized() * 12.f;
            p.timeLeft = 0.4f;
            p.tag = entityId;
            pushes_.push_back(p);
            physics_->Wake(bodies_[s.lever].h);
            continue;
        }
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

void World::SetSteady(int body, const Quat& rot, float maxTorque) {
    for (SteadyRt& s : steady_) if (s.body == body) { s.rot = rot; s.maxTorque = maxTorque; return; }
    steady_.push_back({body, rot, maxTorque});
}

void World::ClearSteady(int body) {
    steady_.erase(std::remove_if(steady_.begin(), steady_.end(), [body](const SteadyRt& s) { return s.body == body; }), steady_.end());
}

float World::SwitchAngle(int entityId) const {
    for (const SwitchRt& s : switches_) if (s.entity == entityId) return physics_->HingeAngle(s.hinge);
    return 0.f;
}

const Part* World::PartOf(int entityId) const {
    const Entity* e = zone_.Find(entityId);
    return e && e->part >= 0 ? &circuit_.GetPart(e->part) : nullptr;
}

int World::EntityOfBody(int body) const { return body >= 0 && body < (int)bodies_.size() ? bodies_[body].entity : 0; }
BodyHandle World::HandleOf(int body) const { return body >= 0 && body < (int)bodies_.size() ? bodies_[body].h : kNoBody; }
Vec3 World::BodyPosition(int body) const { return ToWorld(body, {0, 0, 0}); }

int World::MainBodyOf(int entityId) const {
    const auto it = mainBody_.find(entityId);   // hub: called from scripts, matter, doors, tests every step
    return it == mainBody_.end() ? -1 : it->second;
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

Vec3 World::SurfaceNormal(int body, const Vec3& point) const {
    return body >= 0 && body < (int)bodies_.size() ? physics_->GetHitNormal(bodies_[body].h, point) : Vec3{0, 1, 0};
}

float World::BodyMass(int body) const {
    return body >= 0 && body < (int)bodies_.size() ? physics_->GetMass(bodies_[body].h) : 0.f;
}

int World::AddAgentBody(const BodyDesc& d) {
    BodyDesc a = d;
    a.agent = true;
    const Vec3 draw = d.shape == Shape::Capsule ? Vec3{2 * d.radius, 2 * (d.halfHeight + d.radius), 2 * d.radius} : d.halfExtent * 2.f;
    return AddBody(a, 0, Role::Agent, draw, Rgb{255, 0, 255});
}

void World::SetDrive(int body, const Vec3& target, float maxForce, float frequency, float damping) {
    for (DriveRt& d : drives_) if (d.body == body) { d.target = target; d.maxForce = maxForce; d.freq = frequency; d.damping = damping; return; }
    drives_.push_back({body, target, maxForce, frequency, damping});
    if (body >= 0 && body < (int)bodies_.size()) physics_->Wake(bodies_[body].h);
}

void World::ClearDrive(int body) {
    drives_.erase(std::remove_if(drives_.begin(), drives_.end(), [body](const DriveRt& d) { return d.body == body; }), drives_.end());
}

void World::SetKinematicTarget(int body, const Vec3& pos, const Quat& rot) {
    for (KinRt& k : kinematic_) if (k.body == body) { k.pos = pos; k.rot = rot; return; }
    kinematic_.push_back({body, pos, rot});
}

int World::Attach(int bodyA, int bodyB, const Vec3& worldPoint) {
    if (bodyA < 0 || bodyB < 0 || bodyA >= (int)bodies_.size() || bodyB >= (int)bodies_.size()) return -1;
    const BodyHandle b = physics_->IsDynamic(bodies_[bodyB].h) ? bodies_[bodyB].h : kNoBody;   // static: pin to world
    const ConstraintHandle c = physics_->AddPoint(bodies_[bodyA].h, b, worldPoint);
    joints_.push_back(c);
    physics_->Wake(bodies_[bodyA].h);
    return static_cast<int>(joints_.size()) - 1;
}

void World::Detach(int joint) {
    if (joint < 0 || joint >= (int)joints_.size() || joints_[joint] == kNoConstraint) return;
    physics_->RemoveConstraint(joints_[joint]);
    joints_[joint] = kNoConstraint;
}

void World::BuildWelds() {
    for (const Entity& e : zone_.entities) {
        const auto it = e.params.find("weld");
        if (it == e.params.end()) continue;
        const Entity* to = nullptr;
        if (it->is_number_integer()) to = zone_.Find(it->get<int>());
        else if (it->is_string()) for (const Entity& o : zone_.entities) if (o.name == it->get<std::string>()) { to = &o; break; }
        const int ba = MainBodyOf(e.id), bb = to ? MainBodyOf(to->id) : -1;
        if (!to || ba < 0 || to->id == e.id) { ScriptLogLine(e.name + ": weld target not found"); continue; }
        const BodyHandle ha = bodies_[ba].h, hb = bb >= 0 ? bodies_[bb].h : kNoBody;
        const bool da = physics_->IsDynamic(ha), db = hb != kNoBody && physics_->IsDynamic(hb);
        if (!da && !db) continue;   // two fixed things: nothing to hold
        WeldRt w;
        w.a = e.id; w.b = to->id;
        w.c = da ? physics_->AddFixed(ha, db ? hb : kNoBody) : physics_->AddFixed(hb, kNoBody);
        w.breakN = e.params.value("weldBreakN", 0.f);
        welds_.push_back(w);
    }
    // welded pieces of one object don't collide with each other (they touch or overlap a little)
    std::unordered_map<int, int> root;
    std::function<int(int)> find = [&](int x) { auto it = root.find(x); if (it == root.end()) { root[x] = x; return x; } return it->second == x ? x : (it->second = find(it->second)); };
    for (const WeldRt& w : welds_) root[find(w.a)] = find(w.b);
    std::unordered_map<int, std::vector<BodyHandle>> groups;
    for (auto& [id, r] : root) {
        const int b = MainBodyOf(id);
        if (b >= 0 && physics_->IsDynamic(bodies_[b].h)) groups[find(id)].push_back(bodies_[b].h);
    }
    for (auto& [r, hs] : groups) {
        if (hs.size() < 2) continue;
        const uint32_t g = physics_->NewGroup(static_cast<uint32_t>(hs.size()));
        for (uint32_t i = 0; i < hs.size(); ++i) {
            for (uint32_t j = i + 1; j < hs.size(); ++j) physics_->DisableCollision(g, i, j);
            physics_->SetGroup(hs[i], g, i);
        }
    }
}

void World::StepWelds(float dt) {
    for (WeldRt& w : welds_) {
        if (w.c == kNoConstraint || w.breakN <= 0.f) continue;
        if (physics_->ConstraintForce(w.c, dt) > w.breakN) { physics_->RemoveConstraint(w.c); w.c = kNoConstraint; }
    }
}

bool World::Welded(int a, int b) const {
    for (const WeldRt& w : welds_) if (w.c != kNoConstraint && ((w.a == a && w.b == b) || (w.a == b && w.b == a))) return true;
    return false;
}

float World::JointForce(int joint) const {
    if (joint < 0 || joint >= (int)joints_.size() || joints_[joint] == kNoConstraint) return 0.f;
    return physics_->ConstraintForce(joints_[joint], lastDt_);
}

Vec3 World::BodyVelocity(int body) const {
    return body >= 0 && body < (int)bodies_.size() ? physics_->GetLinearVelocity(bodies_[body].h) : Vec3{};
}

Quat World::BodyRotation(int body) const {
    Vec3 p; Quat q;
    if (body >= 0 && body < (int)bodies_.size()) physics_->GetTransform(bodies_[body].h, p, q);
    return q;
}

void World::BuildRenderFrame(RenderFrame& out) const {
    out.pieces.clear();
    out.lights.clear();
    for (const RtBody& b : bodies_) {
        if (b.h == kNoBody || b.role == Role::Agent || b.role == Role::Terrain) continue;   // removed / drawn by the character renderer / as meshes
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
    AppendMatterPieces(out);
    AppendPlantPieces(out);
    AppendWaterPieces(out);
    out.sunDir = zone_.sunDir.Normalized({0, -1, 0});
    out.sunColor = Vec3{1.f, 0.95f, 0.85f} * (1.6f * zone_.sun);
    out.meshes.clear();
    for (const TerrainRt& t : terrains_) for (const RenderMesh& m : t.meshes) out.meshes.push_back(&m);
    // what scripts set on their objects (a light turned on by logic, a status colour)
    for (RenderPiece& rp : out.pieces) {
        auto e = emissiveOverride_.find(rp.entity);
        if (e != emissiveOverride_.end()) rp.emissive = std::max(rp.emissive, e->second);
        auto c = colorOverride_.find(rp.entity);
        if (c != colorOverride_.end()) rp.color = c->second;
    }
}

} // namespace eon
