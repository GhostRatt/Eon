#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Circuit.h"
#include "Math.h"

namespace eon {

struct Rgb {
    unsigned char r = 200, g = 200, b = 200;
};

// Engine-side description of a thing in the world.
// FIREWALL: name/type/params are internal. Nothing here may ever reach Jenny's mind.
struct Entity {
    int id = 0;
    std::string name;             // e.g. "Battery_9V_A" (editor only)
    std::string type = "static";  // static, battery, wire, lamp, button, switch, motor, resistor
    Vec3 pos;                     // meters, Y up
    Vec3 rotDeg;                  // pitch (x), yaw (y), roll (z)
    Vec3 size{1, 1, 1};           // box extents (meters)
    Rgb color;
    float massKg = 0.f;           // 0 = static
    nlohmann::json params = nlohmann::json::object();

    int part = -1;                // circuit part index (runtime)
};

// Physical electrical contact between terminal `termA` of entity A and terminal `termB` of entity B.
struct TerminalLink {
    int entityA = 0;
    int termA = 0;
    int entityB = 0;
    int termB = 0;
};

struct Zone {
    std::string name;
    float ambientC = 22.f;
    std::vector<Entity> entities;
    std::vector<TerminalLink> links;

    Entity* Find(int id);
    const Entity* Find(int id) const;
    int NextId() const;

    // Creates circuit parts for electrical entities and wires up the links.
    void BuildCircuit(Circuit& circuit);
};

bool IsElectrical(const std::string& type);
Part MakePart(const Entity& e);   // the circuit part an electrical entity becomes

bool LoadZone(const std::string& path, Zone& out, std::string* error = nullptr);

// ---- Object library (prefabs): content/objects/<name>.json ----
// A prefab is a reusable object made of parts (any entity types, with params and scripts) plus its internal wiring.
// A zone places one with an entity of type "prefab" (params.prefab = name; params.set = {"Part.key": value} overrides).
// Expansion turns it into real entities named "<Instance>.<Part>" with stable ids (instance id * 1000 + part #),
// so wires to its parts and saved zones keep working. "$Part" in a part's params means "<Instance>.Part".
struct PrefabInfo { std::string name, description; int parts = 0; };
bool ExpandPrefabs(Zone& zone, const std::string& contentRoot, std::string* log = nullptr);
std::vector<PrefabInfo> ListPrefabs(const std::string& contentRoot);
// Saves entities (and the links among them) as a new prefab, positions relative to `origin`.
bool SavePrefab(const std::string& contentRoot, const std::string& name, const std::string& description,
                const std::vector<Entity>& parts, const std::vector<TerminalLink>& links, const Vec3& origin, std::string* error = nullptr);
bool SaveZone(const std::string& path, const Zone& zone, std::string* error = nullptr);

nlohmann::json ZoneToJson(const Zone& zone);
bool ZoneFromJson(const nlohmann::json& j, Zone& out, std::string* error = nullptr);

} // namespace eon
