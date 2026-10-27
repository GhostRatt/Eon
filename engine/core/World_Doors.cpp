// Doors (hinged panels with a seal and a bolt) and closed air volumes (fridges, ovens).
#include <algorithm>
#include <cmath>

#include "World.h"

namespace eon {

namespace {
float Pf(const nlohmann::json& q, const char* k, float d) { return q.contains(k) && q[k].is_number() ? q[k].get<float>() : d; }
}

void World::BuildDoor(const Entity& e, int mainBody) {
    if (e.params.contains("airC")) {
        ColdRt c;
        c.entity = e.id;
        const Vec3 h = e.size * 0.5f;   // axis-aligned volume (yaw by 90° steps is fine: size is swapped by the author)
        const float yaw = std::fabs(std::fmod(e.rotDeg.y, 180.f));
        const Vec3 hh = (yaw > 45.f && yaw < 135.f) ? Vec3{h.z, h.y, h.x} : h;
        c.lo = e.pos - hh;
        c.hi = e.pos + hh;
        c.target = c.cur = Pf(e.params, "airC", 4.f);
        c.tauShut = Pf(e.params, "tauShut", 600.f);
        c.tauOpen = Pf(e.params, "tauOpen", 90.f);
        const std::string dn = e.params.value("door", std::string());
        for (const Entity& x : zone_.entities) if (!dn.empty() && x.name == dn) c.door = x.id;
        colds_.push_back(c);
    }
    if (e.type != "door" || mainBody < 0 || e.massKg <= 0.f) return;
    DoorRt d;
    d.entity = e.id;
    d.body = mainBody;
    d.swing = Pf(e.params, "swing", 100.f) * 3.14159265f / 180.f;
    d.seal = Pf(e.params, "seal", 0.f);
    d.friction = Pf(e.params, "friction", 0.3f);
    d.locked = e.params.value("locked", false);
    doors_.push_back(d);   // the hinge is made on the first step, once whatever it hangs on exists
}

void World::StepDoors(float dt) {
    for (DoorRt& d : doors_) {
        const Entity* e = zone_.Find(d.entity);
        if (!e) continue;
        if (!d.built) {
            d.built = true;
            BodyHandle frame = kNoBody;
            const std::string fn = e->params.value("frame", std::string());
            for (const Entity& x : zone_.entities)
                if (!fn.empty() && x.name == fn) { const int b = MainBodyOf(x.id); if (b >= 0) frame = bodies_[b].h; }
            Vec3 p; Quat q;
            physics_->GetTransform(bodies_[d.body].h, p, q);
            const bool left = e->params.value("hingeSide", std::string("left")) == "left";
            const Vec3 he = e->size * 0.5f;
            // hinge line on the back vertical edge; axis chosen so that "open" is a positive angle toward local +z
            const Vec3 pivot = p + q.Rotate(Vec3{left ? -he.x : he.x, 0.f, -he.z});
            d.axisW = q.Rotate(Vec3{0, left ? -1.f : 1.f, 0});
            const Vec3 n = q.Rotate(Vec3{left ? 1.f : -1.f, 0, 0});
            d.hinge = physics_->AddHinge(frame, bodies_[d.body].h, pivot, d.axisW, n, n, 0.f, d.swing, d.friction);
        }
        if (d.hinge == kNoConstraint) continue;
        const float a = physics_->HingeAngle(d.hinge);
        // a bolt only goes home in a shut door; once home, the door can't open
        const bool hold = d.locked && a < 0.03f;
        if (hold != d.held) { physics_->SetHingeLimits(d.hinge, 0.f, hold ? 0.f : d.swing); d.held = hold; }
        // seal / magnetic latch: pulls the last few degrees shut, has to be pulled past
        if (d.seal > 0.f && a < 0.09f) {
            physics_->AddTorque(bodies_[d.body].h, d.axisW * (-d.seal * (1.f - a / 0.09f)));
            physics_->Wake(bodies_[d.body].h);
        }
    }
    for (ColdRt& c : colds_) {
        const bool shut = c.door == 0 || DoorShut(c.door);
        const float goal = shut ? c.target : zone_.ambientC;
        const float tau = shut ? c.tauShut : c.tauOpen;
        c.cur += (goal - c.cur) * std::min(1.f, dt / tau);
    }
}

float World::DoorAngle(int id) const {
    for (const DoorRt& d : doors_)
        if (d.entity == id) return d.hinge == kNoConstraint ? 0.f : physics_->HingeAngle(d.hinge) * 57.29578f;
    return 0.f;
}

void World::SetDoorLocked(int id, bool locked) {
    for (DoorRt& d : doors_) if (d.entity == id) d.locked = locked;
}

bool World::DoorLocked(int id) const {
    for (const DoorRt& d : doors_) if (d.entity == id) return d.locked;
    return false;
}

} // namespace eon
