// New map templates: an empty floor, a lit room, or rolling outdoor ground with a pond and plants.
#include <algorithm>
#include <cmath>
#include <random>

#include "MapGen.h"

namespace eon {

namespace {
Entity Box(int id, const std::string& name, const std::string& type, Vec3 pos, Vec3 size, Rgb c, float mass = 0.f, nlohmann::json params = nlohmann::json::object()) {
    Entity e;
    e.id = id; e.name = name; e.type = type; e.pos = pos; e.size = size; e.color = c; e.massKg = mass; e.params = std::move(params);
    return e;
}
}

Zone MakeMap(const NewMapSpec& s) {
    Zone z;
    z.name = s.name;
    z.ambientC = s.ambientC;
    int id = 1;
    if (s.kind == "empty") {
        z.entities.push_back(Box(id++, "Floor", "static", {0, -0.05f, 0}, {s.sizeX, 0.1f, s.sizeZ}, {150, 150, 155}));
        return z;
    }
    if (s.kind == "room") {
        const float w = s.sizeX, d = s.sizeZ, h = s.wallH, t = 0.1f;
        const Rgb floor{150, 140, 128}, wall{205, 200, 190};
        z.entities.push_back(Box(id++, "Floor", "static", {0, -t / 2, 0}, {w, t, d}, floor));
        z.entities.push_back(Box(id++, "Wall_North", "static", {0, h / 2, -d / 2 - t / 2}, {w + 2 * t, h, t}, wall));
        z.entities.push_back(Box(id++, "Wall_South", "static", {0, h / 2, d / 2 + t / 2}, {w + 2 * t, h, t}, wall));
        z.entities.push_back(Box(id++, "Wall_West", "static", {-w / 2 - t / 2, h / 2, 0}, {t, h, d}, wall));
        z.entities.push_back(Box(id++, "Wall_East", "static", {w / 2 + t / 2, h / 2, 0}, {t, h, d}, wall));
        z.entities.push_back(Box(id++, "Ceiling", "static", {0, h + t / 2, 0}, {w + 2 * t, t, d + 2 * t}, wall));
        // a ceiling light on a wall switch, battery and wiring inside the wall
        const int bat = id++, sw = id++, lamp = id++;
        z.entities.push_back(Box(bat, "RoomBattery", "battery", {w / 2 - 0.05f, 1.0f, d / 2 - 0.4f}, {0.06f, 0.15f, 0.1f}, {60, 60, 60}, 0.f,
                                 {{"voltage", 12.0}, {"internalR", 0.05}, {"capacityAh", 40.0}, {"hidden", true}}));
        z.entities.push_back(Box(sw, "LightSwitch", "switch", {w / 2 - 0.01f, 1.15f, d / 2 - 0.6f}, {0.02f, 0.12f, 0.08f}, {245, 245, 245}, 0.f,
                                 {{"axis", nlohmann::json::array({-1, 0, 0})}}));
        z.entities.push_back(Box(lamp, "CeilingLamp", "lamp", {0, h - 0.08f, 0}, {0.12f, 0.1f, 0.12f}, {255, 240, 210}, 0.f, {{"ratedPower", 12.0}, {"ratedVoltage", 12.0}}));
        z.links = {{bat, 0, sw, 0}, {sw, 1, lamp, 0}, {lamp, 1, bat, 1}};
        return z;
    }
    // outdoor
    z.sun = s.sun;
    const int res = std::clamp(static_cast<int>(std::max(s.sizeX, s.sizeZ) * 3.2f) / 4 * 4, 32, 512);
    nlohmann::json tp = {{"res", res}, {"seed", s.seed}, {"amp", s.hills}, {"feature", s.feature}, {"octaves", 5}, {"rough", 0.5}};
    std::mt19937 rng(static_cast<uint32_t>(s.seed) * 7919u + 13u);
    auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    Vec3 pond{U(-0.25f, 0.25f) * s.sizeX, 0, U(-0.25f, 0.25f) * s.sizeZ};
    const float pondR = std::clamp(std::min(s.sizeX, s.sizeZ) * 0.12f, 2.f, 20.f);
    if (s.pond) tp["basins"] = nlohmann::json::array({nlohmann::json::array({pond.x, pond.z, pondR, std::max(0.8f, pondR * 0.28f)})});
    z.entities.push_back(Box(id++, "Ground", "terrain", {0, 0, 0}, {s.sizeX, 4.f, s.sizeZ}, {86, 128, 58}, 0.f, tp));
    if (s.pond)
        z.entities.push_back(Box(id++, "Pond", "water", {pond.x, 0, pond.z}, {pondR * 1.7f, 4.f, pondR * 1.7f}, {70, 110, 140}, 0.f, {{"autoLevel", true}, {"tempC", 15.0}}));
    if (s.plants) {
        id = std::max(id, 100);
        auto away = [&](float x, float zz, float gap) { return !s.pond || std::hypot(x - pond.x, zz - pond.z) > pondR + gap; };
        auto plant = [&](const char* sp, float x, float zz, float growth, int variety = -1) {
            nlohmann::json p = {{"species", sp}, {"growth", growth}};
            if (variety >= 0) p["variety"] = variety;
            z.entities.push_back(Box(id, std::string(sp) + "_" + std::to_string(id), "plant", {x, 0, zz}, {0.1f, 0.1f, 0.1f}, {80, 140, 50}, 0.f, p));
            ++id;
        };
        const float area = s.sizeX * s.sizeZ;
        const int grass = std::min(1500, static_cast<int>(area * 0.14f));
        const float hx = s.sizeX * 0.47f, hz = s.sizeZ * 0.47f;
        for (int i = 0; i < grass; ++i) { const float x = U(-hx, hx), zz = U(-hz, hz); if (away(x, zz, 0.3f)) plant("grass", x, zz, U(0.6f, 1.f)); }
        for (int c = 0; c < std::max(2, static_cast<int>(area / 260)); ++c) {
            const float cx = U(-hx, hx), cz = U(-hz, hz);
            if (!away(cx, cz, 1.f)) continue;
            const int v = static_cast<int>(U(0, 4.99f));
            for (int k = 0; k < 7; ++k) plant("flower", cx + U(-0.8f, 0.8f), cz + U(-0.8f, 0.8f), U(0.7f, 1.f), v);
        }
        struct { const char* sp; float per; float gap; } big[] = {{"bush", 260, 0.8f}, {"berry_bush", 530, 0.8f}, {"tree", 320, 2.f}, {"fruit_tree", 530, 2.f}};
        for (auto& b : big)
            for (int k = 0, n = std::max(1, static_cast<int>(area / b.per)); k < n; ++k) {
                const float x = U(-hx, hx), zz = U(-hz, hz);
                if (away(x, zz, b.gap) && std::hypot(x, zz) > 3.f) plant(b.sp, x, zz, b.sp[0] == 'f' ? 0.95f : 1.f);
            }
    }
    return z;
}

} // namespace eon
