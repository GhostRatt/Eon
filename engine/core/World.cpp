#include "World.h"

#include <algorithm>

namespace eon {

bool World::Load(const std::string& path, std::string* error) {
    Zone z;
    if (!LoadZone(path, z, error)) return false;
    zone_ = std::move(z);
    path_ = path;
    Reset();
    return true;
}

void World::Reset() {
    zone_.BuildCircuit(circuit_);
    accumulator_ = 0.0;
    simTime_ = 0.0;
    circuit_.Step(0.0);
}

void World::Tick(double realDt) {
    accumulator_ += std::clamp(realDt, 0.0, 0.25);
    while (accumulator_ >= kFixedDt) {
        circuit_.Step(kFixedDt);
        accumulator_ -= kFixedDt;
        simTime_ += kFixedDt;
    }
}

void World::SetPressed(int entityId, bool pressed) {
    const Entity* e = zone_.Find(entityId);
    if (e && e->part >= 0 && e->type == "button") circuit_.GetPart(e->part).closed = pressed;
}

void World::Toggle(int entityId) {
    const Entity* e = zone_.Find(entityId);
    if (e && e->part >= 0 && e->type == "switch") {
        Part& p = circuit_.GetPart(e->part);
        p.closed = !p.closed;
    }
}

const Part* World::PartOf(int entityId) const {
    const Entity* e = zone_.Find(entityId);
    return e && e->part >= 0 ? &circuit_.GetPart(e->part) : nullptr;
}

} // namespace eon
