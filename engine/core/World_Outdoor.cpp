// Outdoors: heightfield terrain and bodies of water (buoyancy, containers filling, wet things, cold water).
#include <algorithm>
#include <cmath>

#include "World.h"

namespace eon {

namespace {
float Pf(const nlohmann::json& q, const char* k, float d) { return q.contains(k) && q[k].is_number() ? q[k].get<float>() : d; }
constexpr float kRhoWater = 1000.f;
constexpr float kG = 9.81f;
float Total(const float* ml) { float t = 0.f; for (int i = 0; i < static_cast<int>(Liquid::Count); ++i) t += ml[i]; return t; }
}

bool World::BuildOutdoor(Entity& e) {
    if (e.type == "terrain") {
        TerrainRt t;
        t.entity = e.id;
        const nlohmann::json& q = e.params;
        int res = static_cast<int>(Pf(q, "res", 128.f));
        res = std::clamp((res + 3) / 4 * 4, 8, 1024);   // Jolt wants a multiple of its block size
        t.hf.Resize(res, std::max(1.f, e.size.x), std::max(1.f, e.size.z));
        t.hf.origin = {e.pos.x - 0.5f * t.hf.sizeX, e.pos.y, e.pos.z - 0.5f * t.hf.sizeZ};
        bool loaded = false;
        if (q.contains("heights") && q["heights"].is_string()) loaded = DecodeHeights(q["heights"].get<std::string>(), t.hf);
        if (!loaded) {
            TerrainGen g;
            g.seed = static_cast<uint32_t>(Pf(q, "seed", 1.f));
            g.amplitude = Pf(q, "amp", 2.f);
            g.feature = Pf(q, "feature", 12.f);
            g.octaves = static_cast<int>(Pf(q, "octaves", 5.f));
            g.roughness = Pf(q, "rough", 0.5f);
            GenerateTerrain(t.hf, g);
            for (const auto& b : q.value("basins", nlohmann::json::array()))
                if (b.is_array() && b.size() >= 4) CarveBasin(t.hf, b[0].get<float>(), b[1].get<float>(), b[2].get<float>(), b[3].get<float>());
        }
        terrains_.push_back(std::move(t));
        BuildTerrainBody(terrains_.back());
        return true;
    }
    if (e.type == "water") {
        WaterRt w;
        w.entity = e.id;
        const Vec3 h = e.size * 0.5f;
        w.lo = e.pos - h;
        w.hi = e.pos + h;
        if (e.params.value("autoLevel", false)) {
            // fill the hollow it sits in: surface a little below the lowest point of the ground around its edge
            float rim = 1e9f, bed = 1e9f, g;
            for (int k = 0; k < 64; ++k) {
                const float t = k / 64.f;
                const float x = k < 16 ? w.lo.x + (w.hi.x - w.lo.x) * t * 4 : k < 32 ? w.hi.x : k < 48 ? w.hi.x - (w.hi.x - w.lo.x) * (t - 0.5f) * 4 : w.lo.x;
                const float z = k < 16 ? w.lo.z : k < 32 ? w.lo.z + (w.hi.z - w.lo.z) * (t - 0.25f) * 4 : k < 48 ? w.hi.z : w.hi.z - (w.hi.z - w.lo.z) * (t - 0.75f) * 4;
                if (GroundHeight(x, z, g)) rim = std::min(rim, g);
            }
            for (int k = 0; k < 15; ++k)
                for (int m = 0; m < 15; ++m)
                    if (GroundHeight(w.lo.x + (w.hi.x - w.lo.x) * (k + 0.5f) / 15.f, w.lo.z + (w.hi.z - w.lo.z) * (m + 0.5f) / 15.f, g)) bed = std::min(bed, g);
            if (rim < 1e8f && bed < 1e8f) {
                w.hi.y = rim - e.params.value("freeboard", 0.12f);
                w.lo.y = bed - 0.05f;
            }
        }
        w.tempC = Pf(e.params, "tempC", 16.f);
        w.color = e.color;
        waters_.push_back(w);
        for (TerrainRt& t : terrains_) BuildTerrainBody(t);   // shoreline colours depend on the water level
        return true;
    }
    return false;
}

void World::BuildTerrainBody(TerrainRt& t) {
    if (t.body >= 0 && t.body < static_cast<int>(bodies_.size()) && bodies_[t.body].h != kNoBody) {
        physics_->RemoveBody(bodies_[t.body].h);
        bodies_[t.body].h = kNoBody;
    }
    BodyDesc d;
    d.shape = Shape::HeightField;
    d.pos = t.hf.origin;
    d.samples = t.hf.h.data();
    d.sampleCount = t.hf.n;
    d.cellX = t.hf.CellX();
    d.cellZ = t.hf.CellZ();
    d.friction = 0.8f;
    if (t.body >= 0 && t.body < static_cast<int>(bodies_.size())) {
        // reuse its slot so body indices (and the main-body map) stay valid
        d.userData = static_cast<uint64_t>(t.body) + 1;
        bodies_[t.body].h = physics_->CreateBody(d);
    } else {
        t.body = AddBody(d, t.entity, Role::Terrain, {t.hf.sizeX, 0.f, t.hf.sizeZ}, {86, 128, 58});
    }
    t.meshes.clear();
    std::vector<WaterBox> wb;
    for (const WaterRt& w : waters_) wb.push_back({w.lo, w.hi});
    BuildTerrainMeshes(t.hf, wb, t.entity, ++t.version, t.meshes);
}

float World::LowestWaterY() const {
    float y = -1e9f;
    for (const WaterRt& w : waters_) y = y < -1e8f ? w.hi.y : std::min(y, w.hi.y);
    return y;
}

bool World::GroundHeight(float x, float z, float& y) const {
    for (const TerrainRt& t : terrains_)
        if (t.hf.Inside(x, z)) { y = t.hf.HeightAt(x, z); return true; }
    return false;
}

float World::WaterDepth(const Vec3& p) const {
    float best = -1e9f;
    for (const WaterRt& w : waters_) {
        if (p.x < w.lo.x || p.x > w.hi.x || p.z < w.lo.z || p.z > w.hi.z || p.y < w.lo.y) continue;
        float d = w.hi.y - p.y;
        // the volume may be a big box over uneven ground: under the terrain isn't "in the water"
        float g;
        if (GroundHeight(p.x, p.z, g) && p.y < g) continue;
        best = std::max(best, d);
    }
    return best < -1e8f ? -1.f : best;
}

float World::WaterTemp(const Vec3& p) const {
    for (const WaterRt& w : waters_)
        if (p.x >= w.lo.x && p.x <= w.hi.x && p.z >= w.lo.z && p.z <= w.hi.z && p.y >= w.lo.y && p.y <= w.hi.y) return w.tempC;
    return zone_.ambientC;
}

Heightfield* World::TerrainField(int entityId) {
    for (TerrainRt& t : terrains_) if (entityId == 0 || t.entity == entityId) return &t.hf;
    return nullptr;
}

void World::TerrainChanged(int entityId, bool saveToZone) {
    for (TerrainRt& t : terrains_) {
        if (entityId != 0 && t.entity != entityId) continue;
        BuildTerrainBody(t);
        if (saveToZone) if (Entity* e = zone_.Find(t.entity)) e->params["heights"] = EncodeHeights(t.hf);
        if (entityId == 0) break;
    }
}

void World::StepWater(float dt) {
    if (waters_.empty()) return;
    // buoyancy and drag on anything loose in the water: the box is split into 2x2x2 cells, each pushed up at its own
    // centre by the water it displaces, so a log lies flat and a tipped box rights itself
    for (const RtBody& b : bodies_) {
        if (b.h == kNoBody || b.role == Role::Agent || b.role == Role::Terrain || !physics_->IsDynamic(b.h)) continue;
        Vec3 p; Quat q;
        physics_->GetTransform(b.h, p, q);
        const float reach = 0.5f * std::max({b.size.x, b.size.y, b.size.z});
        if (WaterDepth(p) < -reach) continue;
        const Vec3 ax = q.Rotate({1, 0, 0}), ay = q.Rotate({0, 1, 0}), az = q.Rotate({0, 0, 1});
        const float cellHalf = 0.25f * (std::fabs(ax.y) * b.size.x + std::fabs(ay.y) * b.size.y + std::fabs(az.y) * b.size.z);
        const float cellVol = b.size.x * b.size.y * b.size.z / 8.f;
        const float m = physics_->GetMass(b.h);
        float wetFrac = 0.f;
        for (int k = 0; k < 8; ++k) {
            const Vec3 c = p + ax * (((k & 1) ? 0.25f : -0.25f) * b.size.x) + ay * (((k & 2) ? 0.25f : -0.25f) * b.size.y) + az * (((k & 4) ? 0.25f : -0.25f) * b.size.z);
            const float frac = std::clamp((WaterDepth(c) + cellHalf) / std::max(1e-4f, 2.f * cellHalf), 0.f, 1.f);
            if (frac <= 0.f) continue;
            wetFrac += frac / 8.f;
            const Vec3 v = physics_->GetVelocityAt(b.h, c);
            physics_->AddForce(b.h, Vec3{0, kRhoWater * kG * cellVol * frac, 0} + v * (-0.5f * m * frac), c);
        }
        if (wetFrac <= 0.f) continue;
        const Vec3 w = physics_->GetAngularVelocity(b.h);
        physics_->SetAngularVelocity(b.h, w * std::max(0.f, 1.f - 1.5f * wetFrac * dt));
        physics_->Wake(b.h);
    }
    // a container pushed under fills up
    for (ContainerRt& c : containers_) {
        Vec3 cp; Quat cq;
        physics_->GetTransform(bodies_[c.body].h, cp, cq);
        const Vec3 rim = cp + cq.Rotate(Vec3{0, c.half.y, 0});
        if (WaterDepth(rim) <= 0.f || cq.Rotate({0, 1, 0}).y < 0.2f) continue;
        const float room = std::max(0.f, c.capacity - Total(c.ml));
        c.ml[static_cast<int>(Liquid::Water)] += std::min(room, 250.f * dt);
    }
}

void World::AppendWaterPieces(RenderFrame& out) const {
    for (const WaterRt& w : waters_) {
        RenderPiece rp;
        rp.pos = {0.5f * (w.lo.x + w.hi.x), w.hi.y - 0.01f, 0.5f * (w.lo.z + w.hi.z)};
        rp.size = {w.hi.x - w.lo.x, 0.02f, w.hi.z - w.lo.z};
        rp.color = w.color;
        rp.alpha = 0.55f;
        rp.entity = w.entity;
        out.pieces.push_back(rp);
    }
}

} // namespace eon
