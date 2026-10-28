// Plants: grass, flowers, bushes, trees, fruit trees and berry bushes that grow with water and light, wilt when
// dry, grow fruit on stems (pick it by pulling), drop overripe fruit, which rots into a seed that sprouts.
#include <algorithm>
#include <cmath>

#include "World.h"

namespace eon {

namespace {
float Pf(const nlohmann::json& q, const char* k, float d) { return q.contains(k) && q[k].is_number() ? q[k].get<float>() : d; }
float Total(const float* ml) { float t = 0.f; for (int i = 0; i < static_cast<int>(Liquid::Count); ++i) t += ml[i]; return t; }

uint32_t Next(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
float Rand(uint32_t& s) { return static_cast<float>(Next(s) & 0xffffff) / 16777215.f; }

struct SpeciesSpec {
    const char* name;
    float daysToGrow;     // to full size with plenty of water and light
    float height;         // m at full size
    float spread;         // m (radius of canopy / patch)
    float thirst;         // moisture used per day
    bool solid;           // has a body she bumps into
    float fruitEvery;     // days between fruits (0: none)
    int maxFruit;
    float fruitG;         // grams of one fruit
    Rgb fruitRipe;
};
const SpeciesSpec kSpec[] = {
    {"grass", 6.f, 0.28f, 0.18f, 0.25f, false, 0.f, 0, 0.f, {}},
    {"flower", 12.f, 0.42f, 0.08f, 0.3f, false, 0.f, 0, 0.f, {}},
    {"bush", 40.f, 0.9f, 0.55f, 0.35f, true, 0.f, 0, 0.f, {}},
    {"berry_bush", 35.f, 0.8f, 0.5f, 0.35f, true, 1.5f, 10, 4.f, {150, 30, 60}},
    {"tree", 180.f, 5.5f, 2.0f, 0.1f, true, 0.f, 0, 0.f, {}},          // deep roots: trees are slow to dry out
    {"fruit_tree", 120.f, 3.6f, 1.6f, 0.12f, true, 2.5f, 8, 150.f, {200, 40, 35}},
};
const SpeciesSpec& Spec(World::Species s) { return kSpec[static_cast<int>(s)]; }
bool SpeciesFrom(const std::string& n, World::Species& out) {
    for (int i = 0; i < 6; ++i) if (n == kSpec[i].name) { out = static_cast<World::Species>(i); return true; }
    return false;
}
Rgb Mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {static_cast<unsigned char>(a.r + (b.r - a.r) * t), static_cast<unsigned char>(a.g + (b.g - a.g) * t), static_cast<unsigned char>(a.b + (b.b - a.b) * t)};
}
}   // namespace

bool World::BuildPlant(Entity& e) {
    if (e.type == "seed") {
        Species sp = Species::Grass;
        SpeciesFrom(e.params.value("species", std::string("grass")), sp);
        SeedRt s;
        s.entity = e.id;
        s.species = sp;
        // a seed is a real little thing: it falls, rolls, she can pick it up
        BodyDesc d;
        d.pos = e.pos;
        d.halfExtent = {0.006f, 0.004f, 0.006f};
        d.mass = 0.001f;
        d.fastMoving = true;
        d.friction = 0.9f;
        AddBody(d, e.id, Role::Main, {0.012f, 0.008f, 0.012f}, e.color);
        seeds_.push_back(s);
        return true;
    }
    if (e.type != "plant") return false;
    PlantRt p;
    p.entity = e.id;
    SpeciesFrom(e.params.value("species", std::string("grass")), p.species);
    p.growth = std::clamp(Pf(e.params, "growth", 1.f), 0.02f, 1.f);
    p.moisture = Pf(e.params, "moisture", 0.6f);
    p.health = Pf(e.params, "health", 1.f);
    p.rng = static_cast<uint32_t>(e.id) * 2654435761u + 7u;
    static const Rgb blooms[] = {{220, 50, 70}, {245, 210, 60}, {150, 80, 200}, {245, 245, 240}, {250, 140, 40}};
    p.bloom = blooms[static_cast<int>(Pf(e.params, "variety", static_cast<float>(e.id % 5))) % 5];
    // its roots are where the ground is
    p.base = e.pos;
    float g;
    if (GroundHeight(e.pos.x, e.pos.z, g)) p.base.y = g;
    else {
        RayHit hit;
        if (RayCast(e.pos + Vec3{0, 2.f, 0}, {0, -1, 0}, 6.f, hit, true)) p.base.y = hit.point.y;
    }
    plants_.push_back(p);
    BuildPlantBody(plants_.back());
    return true;
}

void World::BuildPlantBody(PlantRt& p) {
    const SpeciesSpec& sp = Spec(p.species);
    if (!sp.solid) return;
    const float size = p.growth;
    if (p.builtSize >= 0.f && std::fabs(size - p.builtSize) < 0.08f) return;
    p.builtSize = size;
    BodyDesc d;
    const bool tree = p.species == Species::Tree || p.species == Species::FruitTree;
    if (tree) {
        // the trunk (the canopy is above her head and only leaves)
        const float h = sp.height * size * 0.55f, r = std::max(0.03f, 0.12f * size);
        d.halfExtent = {r, std::max(0.05f, 0.5f * h), r};
        d.pos = p.base + Vec3{0, d.halfExtent.y, 0};
    } else {
        const float h = std::max(0.08f, sp.height * size * 0.8f), r = std::max(0.06f, sp.spread * size * 0.8f);
        d.halfExtent = {r, 0.5f * h, r};
        d.pos = p.base + Vec3{0, 0.5f * h, 0};
        d.friction = 0.9f;
    }
    if (p.body >= 0 && p.body < static_cast<int>(bodies_.size()) && bodies_[p.body].h != kNoBody) {
        physics_->RemoveBody(bodies_[p.body].h);
        d.userData = static_cast<uint64_t>(p.body) + 1;
        bodies_[p.body].h = physics_->CreateBody(d);
        bodies_[p.body].size = d.halfExtent * 2.f;
    } else {
        p.body = AddBody(d, p.entity, Role::Main, d.halfExtent * 2.f, {90, 70, 50});
    }
    bodies_[p.body].role = Role::Terrain;   // drawn as a plant, not as its collision box
}

float World::LightAt(const Vec3& p) const {
    float l = zone_.sun;
    for (const Entity& e : zone_.entities) {
        if (e.type != "lamp" || e.part < 0) continue;
        const Part& part = circuit_.GetPart(e.part);
        if (part.brightness <= 0.01) continue;
        const float d = Distance(e.pos, p);
        l += static_cast<float>(part.brightness) * 0.8f / (1.f + d * d);
    }
    return std::clamp(l, 0.f, 1.f);
}

void World::SoilWaterAt(const Vec3& p, float radius, float& table, float& soakedOut) {
    // standing water close by keeps roots wet; puddles soak into the ground
    float wet = 0.f;
    for (const WaterRt& w : waters_) {
        const float dx = std::max({w.lo.x - p.x, 0.f, p.x - w.hi.x}), dz = std::max({w.lo.z - p.z, 0.f, p.z - w.hi.z});
        wet = std::max(wet, std::clamp(1.f - std::sqrt(dx * dx + dz * dz) / (radius + 1.5f), 0.f, 1.f));
    }
    float soaked = 0.f;
    for (PuddleRt& q : puddles_) {
        const float d = Distance(q.pos, p);
        if (d > radius + 0.3f) continue;
        const float tot = Total(q.ml);
        const float take = std::min(tot * 0.35f, 50.f);   // ml per call: several roots share a puddle
        if (take <= 0.f) continue;
        for (float& m : q.ml) m -= take * (tot > 0.f ? m / tot : 0.f);
        soaked += take;
    }
    table = wet;
    soakedOut = soaked / 400.f;
}

void World::StepPlants(float dt) {
    if (plants_.empty() && seeds_.empty() && fruits_.empty()) return;
    plantClock_ += dt;
    if (plantClock_ < 0.25) return;   // nature is slow: 4 Hz is plenty
    const float days = static_cast<float>(plantClock_) / std::max(1.f, zone_.dayLength);
    const float gdays = days * zone_.growth;
    plantClock_ = 0.0;

    // seeds first (rain/watering wets the topsoil they lie in before roots take it): resting on moist ground for a couple of days, they sprout
    for (size_t i = 0; i < seeds_.size(); ++i) {
        SeedRt& s = seeds_[i];
        const int b = MainBodyOf(s.entity);
        if (b < 0 || !zone_.Find(s.entity)) { s.entity = 0; continue; }
        const Vec3 at = BodyPosition(b);
        if (physics_->GetLinearVelocity(bodies_[b].h).Length() > 0.02f) continue;
        RayHit g;
        if (!RayCast(at - Vec3{0, 0.0055f, 0}, {0, -1, 0}, 0.03f, g, true) || BodyMass(g.body) > 0.f) continue;   // on the ground, not on a thing (ray starts under the seed)
        float table = 0.f, soaked = 0.f;
        SoilWaterAt(at, 0.15f, table, soaked);
        // a seed only needs the few cm of soil around it wet (a cup of water soaks that), and it stays damp for days
        s.moisture = std::clamp(std::max(s.moisture + soaked * 3.f - 0.15f * gdays, table), 0.f, 1.f);
        if (s.moisture > 0.25f) s.moistDays += gdays;
        if (s.moistDays >= 2.f) {
            Entity p;
            p.type = "plant";
            p.name = std::string(Spec(s.species).name) + "_sprout";
            p.pos = at;
            p.params = {{"species", Spec(s.species).name}, {"growth", 0.03}, {"moisture", s.moisture}};
            const int sid = s.entity;
            s.entity = 0;
            Despawn(sid);
            p.id = zone_.NextId();
            zone_.entities.push_back(p);   // a real plant now (saved with the zone)
            BuildPlant(zone_.entities.back());
        }
    }
    seeds_.erase(std::remove_if(seeds_.begin(), seeds_.end(), [](const SeedRt& s) { return s.entity == 0; }), seeds_.end());

    for (size_t i = 0; i < plants_.size(); ++i) {
        PlantRt& p = plants_[i];
        const SpeciesSpec& sp = Spec(p.species);
        // water: what the roots can reach, minus what it uses (more in sun and when big)
        float table = 0.f, soaked = 0.f;
        SoilWaterAt(p.base, sp.spread * std::max(0.3f, p.growth), table, soaked);
        const float light = LightAt(p.base + Vec3{0, sp.height * p.growth, 0});
        // use: more in sun and the bigger it is; supply: puddles soaking in, and the water table near a pond
        p.moisture -= sp.thirst * gdays * (0.5f + 0.5f * light) * (0.4f + 0.6f * p.growth);
        p.moisture += soaked;
        if (table > p.moisture) p.moisture += (table - p.moisture) * std::min(1.f, gdays * 4.f);
        p.moisture = std::clamp(p.moisture, 0.f, 1.f);
        if (p.dead) continue;
        // health: wilts when the soil is dry, recovers when watered
        if (p.moisture < 0.12f) p.health = std::max(0.f, p.health - 0.25f * gdays);   // ~4 dry days and it's gone
        else p.health = std::min(1.f, p.health + 0.8f * gdays * p.moisture);
        if (p.health <= 0.f) { p.dead = true; continue; }
        // growth: logistic-ish, limited by water and light
        const float wf = std::clamp(p.moisture / 0.35f, 0.f, 1.f), lf = 0.15f + 0.85f * light;
        p.growth = std::min(1.f, p.growth + gdays / sp.daysToGrow * wf * lf * p.health * (0.4f + 1.2f * p.growth * (1.f - p.growth) + 0.3f));
        BuildPlantBody(p);
        // fruit: a grown, healthy fruiting plant sets fruit now and then, on a stem out in its canopy
        if (sp.fruitEvery > 0.f && p.growth > 0.75f && p.health > 0.6f && p.moisture > 0.1f) {
            p.fruitTimer += gdays;
            int hanging = 0;
            for (const FruitRt& f : fruits_) if (f.onPlant && f.from == p.species && f.joint >= 0) {
                const Entity* fe = zone_.Find(f.entity);
                if (fe && fe->params.value("plant", 0) == p.entity) ++hanging;
            }
            if (p.fruitTimer >= sp.fruitEvery && hanging < sp.maxFruit) {
                p.fruitTimer = 0.f;
                const bool tree = p.species == Species::FruitTree;
                const float a = Rand(p.rng) * 6.2831853f, rr = (tree ? 0.55f + 0.4f * Rand(p.rng) : 0.6f + 0.3f * Rand(p.rng)) * sp.spread * p.growth;
                const float hy = tree ? sp.height * p.growth * (0.42f + 0.3f * Rand(p.rng)) : sp.height * p.growth * (0.35f + 0.4f * Rand(p.rng));   // low branches too: reachable
                const Vec3 stem = p.base + Vec3{std::cos(a) * rr, hy, std::sin(a) * rr};
                Entity fe;
                fe.type = "food";
                fe.name = tree ? "fruit" : "berry";
                const float sz = tree ? 0.075f : 0.016f;
                fe.size = {sz, sz, sz};
                fe.pos = stem - Vec3{0, 0.5f * sz + 0.01f, 0};
                fe.massKg = sp.fruitG / 1000.f;
                fe.color = {110, 160, 60};   // green until it ripens
                fe.params = {{"grams", sp.fruitG}, {"kcalPerG", tree ? 0.52 : 0.45}, {"waterPerG", tree ? 0.85 : 0.82}, {"plant", p.entity},
                             {"inside", tree ? nlohmann::json::array({240, 232, 190}) : nlohmann::json::array({120, 20, 40})}};
                const int id = Spawn(fe);
                if (id > 0) {
                    FruitRt f;
                    f.entity = id;
                    f.from = p.species;
                    // the stem: a point joint to the plant (static) above the fruit, so it hangs and swings
                    const int fb = MainBodyOf(id);
                    if (fb >= 0) f.joint = Attach(fb, p.body >= 0 ? p.body : fb, stem);
                    fruits_.push_back(f);
                }
            }
        }
    }

    // fruit: ripens on the stem, can be pulled off, falls when overripe, rots on the ground into a seed
    for (size_t i = 0; i < fruits_.size(); ++i) {
        FruitRt& f = fruits_[i];
        const Entity* e = zone_.Find(f.entity);
        if (!e) { f.entity = 0; continue; }
        const float before = f.age;
        f.age += gdays;
        const SpeciesSpec& sp = Spec(f.from);
        const float ripe = f.age / 3.f;   // 3 days to ripen
        if (!f.rotten) {
            Rgb c = ripe < 1.f ? Mix({110, 160, 60}, sp.fruitRipe, ripe) : Mix(sp.fruitRipe, {90, 60, 35}, (ripe - 1.f) / 2.f);
            SetColor(f.entity, c);
        }
        if (f.onPlant && f.joint >= 0) {
            // pulled (a hand, a shove) harder than the stem holds, or overripe: it comes off
            const float hold = std::max(1.5f, 6.f * (1.f - std::clamp(ripe - 0.8f, 0.f, 1.f) * 0.7f));
            if (JointForce(f.joint) > hold + 9.81f * e->massKg || ripe > 1.6f) {
                Detach(f.joint);
                f.joint = -1;
                f.onPlant = false;
            }
        } else if (!f.onPlant) {
            // lying on the ground (not in her hand): it slowly rots
            const int b = MainBodyOf(f.entity);
            if (b >= 0 && physics_->GetLinearVelocity(bodies_[b].h).Length() < 0.05f) f.lyingDays += gdays;
            if (!f.rotten && f.lyingDays > 2.f) { f.rotten = true; SetColor(f.entity, {80, 55, 35}); }
            if (f.rotten && f.lyingDays > 4.f && FoodLeftG(f.entity) > 0.f) {
                // what's left goes back to the ground: a seed where it lay
                const Vec3 at = b >= 0 ? BodyPosition(b) : e->pos;
                Entity s;
                s.type = "seed";
                s.name = "seed";
                s.pos = at + Vec3{0, 0.01f, 0};
                s.color = {70, 50, 30};
                s.params = {{"species", Spec(f.from).name}};
                const int fid = f.entity;
                f.entity = 0;
                Despawn(fid);
                const int sid = Spawn(s);
                if (sid > 0) {
                    SeedRt sd;
                    sd.entity = sid;
                    sd.species = f.from;
                    seeds_.push_back(sd);
                }
            }
        }
        (void)before;
    }
    fruits_.erase(std::remove_if(fruits_.begin(), fruits_.end(), [](const FruitRt& f) { return f.entity == 0; }), fruits_.end());

}

void World::AppendPlantPieces(RenderFrame& out) const {
    for (const PlantRt& p : plants_) {
        const SpeciesSpec& sp = Spec(p.species);
        uint32_t r = p.rng;
        const float g = p.growth;
        // colour: green when healthy, yellowing when thirsty, brown when dead
        const float wilt = p.dead ? 1.f : std::clamp((1.f - p.health) * 1.3f, 0.f, 1.f);
        const Rgb leafBase = p.species == Species::Grass ? Rgb{80, 140, 50} : p.species == Species::Tree ? Rgb{50, 110, 45} : Rgb{60, 125, 50};
        const Rgb leaf = Mix(Mix(leafBase, {170, 160, 70}, wilt), {110, 85, 50}, p.dead ? 1.f : 0.f);
        const float droop = wilt * 0.6f;   // wilting stems lean over
        auto piece = [&](const Vec3& pos, const Vec3& size, Rgb c, const Quat& q = Quat{}) {
            RenderPiece rp;
            rp.pos = pos;
            rp.size = size;
            rp.color = c;
            rp.rot = q;
            rp.entity = p.entity;
            out.pieces.push_back(rp);
        };
        auto jitter = [&](Rgb c) {
            const int j = static_cast<int>(Rand(r) * 24.f) - 12;
            return Rgb{static_cast<unsigned char>(std::clamp(c.r + j, 0, 255)), static_cast<unsigned char>(std::clamp(c.g + j, 0, 255)), static_cast<unsigned char>(std::clamp(c.b + j / 2, 0, 255))};
        };
        switch (p.species) {
        case Species::Grass: {
            const int blades = 9;
            for (int k = 0; k < blades; ++k) {
                const float a = Rand(r) * 6.2831853f, d = Rand(r) * sp.spread;
                const float h = sp.height * g * (0.5f + 0.6f * Rand(r));
                const Vec3 at = p.base + Vec3{std::cos(a) * d, 0.5f * h, std::sin(a) * d};
                const Quat q = Quat::AxisAngle({0, 1, 0}, Rand(r) * 3.14f) * Quat::AxisAngle({1, 0, 0}, (Rand(r) - 0.5f) * 0.5f + droop);
                piece(at, {0.012f, h, 0.003f}, jitter(leaf), q);
            }
            break;
        }
        case Species::Flower: {
            const float h = sp.height * g;
            const Quat lean = Quat::AxisAngle({1, 0, 0}, droop * 1.2f);
            piece(p.base + lean.Rotate({0, 0.5f * h, 0}), {0.006f, h, 0.006f}, leaf, lean);
            for (int k = 0; k < 2; ++k) {
                const float a = k * 3.14159f + Rand(r);
                piece(p.base + Vec3{std::cos(a) * 0.035f, 0.3f * h, std::sin(a) * 0.035f}, {0.06f, 0.004f, 0.025f}, leaf, Quat::AxisAngle({0, 1, 0}, a));
            }
            if (g > 0.55f && !p.dead) {   // in bloom
                const Vec3 head = p.base + lean.Rotate({0, h, 0});
                const float pr = 0.025f * std::min(1.f, (g - 0.5f) * 2.f) + 0.01f;
                const Rgb petal = Mix(p.bloom, {150, 120, 80}, wilt);
                for (int k = 0; k < 5; ++k) {
                    const float a = k * 1.2566f;
                    piece(head + Vec3{std::cos(a) * pr, 0.f, std::sin(a) * pr}, {pr * 1.3f, 0.004f, pr * 0.8f}, petal, Quat::AxisAngle({0, 1, 0}, -a));
                }
                piece(head + Vec3{0, 0.003f, 0}, {pr * 0.7f, 0.008f, pr * 0.7f}, {235, 190, 40});
            }
            break;
        }
        case Species::Bush:
        case Species::BerryBush: {
            const float s = sp.spread * g, h = sp.height * g * (1.f - droop * 0.4f);
            piece(p.base + Vec3{0, 0.08f * g, 0}, {0.05f * g + 0.01f, 0.16f * g, 0.05f * g + 0.01f}, {95, 75, 50});
            const int blobs = 9;
            for (int k = 0; k < blobs; ++k) {
                const float a = Rand(r) * 6.2831853f, d = Rand(r) * s * 0.7f;
                const float y = h * (0.35f + 0.5f * Rand(r));
                const float b = s * (0.55f + 0.4f * Rand(r));
                piece(p.base + Vec3{std::cos(a) * d, y, std::sin(a) * d}, {b, b * 0.8f, b}, jitter(leaf), Quat::AxisAngle({0, 1, 0}, Rand(r) * 3.f));
            }
            break;
        }
        case Species::Tree:
        case Species::FruitTree: {
            const bool fruit = p.species == Species::FruitTree;
            const float h = sp.height * g, trunkH = h * 0.55f, tr = std::max(0.03f, 0.12f * g);
            const Rgb bark = {100, 78, 55};
            piece(p.base + Vec3{0, 0.5f * trunkH, 0}, {tr * 2.f, trunkH, tr * 2.f}, bark);
            // a few branches up into the crown
            for (int k = 0; k < 4; ++k) {
                const float a = k * 1.5708f + Rand(r) * 0.6f;
                const Vec3 dir = Vec3{std::cos(a) * 0.7f, 0.7f, std::sin(a) * 0.7f}.Normalized();
                const float len = sp.spread * g * 0.6f;
                piece(p.base + Vec3{0, trunkH * 0.85f, 0} + dir * (0.5f * len), {tr, len, tr}, bark, Quat::FromTo({0, 1, 0}, dir));
            }
            // crown: leaf masses (thinner and browner when dry; bare when dead)
            if (!p.dead) {
                const int blobs = fruit ? 10 : 12;
                const float cr = sp.spread * g;
                for (int k = 0; k < blobs; ++k) {
                    const float a = Rand(r) * 6.2831853f, d = Rand(r) * cr * 0.75f;
                    const float y = trunkH + (h - trunkH) * (fruit ? 0.25f + 0.6f * Rand(r) : 0.2f + 0.75f * Rand(r));
                    const float b = cr * (fruit ? 0.6f : 0.5f) * (0.6f + 0.5f * Rand(r)) * (1.f - wilt * 0.3f);
                    piece(p.base + Vec3{std::cos(a) * d, y, std::sin(a) * d}, {b, b * 0.75f, b}, jitter(leaf), Quat::AxisAngle({0, 1, 0}, Rand(r) * 3.f));
                }
            }
            break;
        }
        }
    }
}

bool World::PlantState(int id, PlantInfo& out) const {
    for (const PlantRt& p : plants_) {
        if (p.entity != id) continue;
        out.growth = p.growth;
        out.health = p.health;
        out.moisture = p.moisture;
        out.dead = p.dead;
        out.fruits = 0;
        for (const FruitRt& f : fruits_) {
            const Entity* e = zone_.Find(f.entity);
            if (e && f.onPlant && e->params.value("plant", 0) == id) ++out.fruits;
        }
        return true;
    }
    return false;
}

float World::FruitRipeness(int id) const {
    for (const FruitRt& f : fruits_) if (f.entity == id) return f.age / 3.f;
    return -1.f;
}

bool World::FruitOnPlant(int id) const {
    for (const FruitRt& f : fruits_) if (f.entity == id) return f.onPlant;
    return false;
}

int World::ScatterPlants(const std::string& species, const Vec3& center, float radius, int count, uint32_t seed, float growth) {
    Species sp;
    if (!SpeciesFrom(species, sp)) return 0;
    uint32_t r = seed * 2654435761u + 1u;
    int made = 0;
    const float minGap = Spec(sp).spread * 0.6f;
    for (int k = 0; k < count * 4 && made < count; ++k) {
        const float a = Rand(r) * 6.2831853f, d = std::sqrt(Rand(r)) * radius;
        Vec3 at = center + Vec3{std::cos(a) * d, 0.f, std::sin(a) * d};
        float gy;
        if (GroundHeight(at.x, at.z, gy)) at.y = gy;
        else {
            RayHit hit;
            if (!RayCast(at + Vec3{0, 3.f, 0}, {0, -1, 0}, 8.f, hit, true)) continue;
            at.y = hit.point.y;
        }
        if (WaterDepth(at + Vec3{0, 0.02f, 0}) > 0.f) continue;   // not in the pond
        bool crowded = false;
        for (const PlantRt& p : plants_) crowded |= p.species == sp && Distance(p.base, at) < minGap;
        if (crowded) continue;
        Entity e;
        e.id = zone_.NextId();
        e.type = "plant";
        e.name = species + "_" + std::to_string(e.id);
        e.pos = at;
        e.size = {0.1f, 0.1f, 0.1f};
        e.params = {{"species", species}, {"growth", std::clamp(growth * (0.75f + 0.25f * Rand(r)), 0.02f, 1.f)}, {"variety", static_cast<int>(Rand(r) * 5.f)}};
        zone_.entities.push_back(e);
        BuildPlant(zone_.entities.back());
        ++made;
    }
    return made;
}

} // namespace eon
