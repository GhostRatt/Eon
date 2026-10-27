#include "Zone.h"
#include <unordered_map>
#include <filesystem>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace eon {

using nlohmann::json;

namespace {

json V3(const Vec3& v) { return json::array({v.x, v.y, v.z}); }

Vec3 ReadV3(const json& j, const char* key, Vec3 def) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3) return def;
    return {j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>()};
}

double P(const json& params, const char* key, double def) {
    return params.contains(key) && params[key].is_number() ? params[key].get<double>() : def;
}

Part MakePart(const Entity& e) {
    Part p;
    const json& q = e.params;
    if (e.type == "battery") {
        p.kind = PartKind::Battery;
        p.voltage = P(q, "voltage", 9.0);
        p.internalR = P(q, "internalR", 1.5);
        p.capacityAh = P(q, "capacityAh", 0.5);
        p.chargeAh = p.capacityAh * std::clamp(P(q, "charge", 1.0), 0.0, 1.0);
    } else if (e.type == "wire") {
        p.kind = PartKind::Wire;
        const double length = P(q, "length", std::max({e.size.x, e.size.y, e.size.z}));
        p.resistance = P(q, "resistance", 0.02 * length); // ~22 AWG copper
    } else if (e.type == "lamp") {
        p.kind = PartKind::Lamp;
        p.ratedPower = P(q, "ratedPower", 5.0);
        const double ratedV = P(q, "ratedVoltage", 12.0);
        p.resistance = P(q, "resistance", ratedV * ratedV / p.ratedPower);
    } else if (e.type == "button" || e.type == "switch" || e.type == "lever" || e.type == "relay") {
        p.kind = PartKind::Switch;
        p.closed = q.value("closed", false);
    } else if (e.type == "heater") {
        // a resistive element: hot when current flows through it
        p.kind = PartKind::Resistor;
        p.ratedPower = P(q, "ratedPower", 120.0);
        const double ratedV = P(q, "ratedVoltage", 12.0);
        p.resistance = P(q, "resistance", ratedV * ratedV / p.ratedPower);
    } else if (e.type == "motor") {
        p.kind = PartKind::Motor;
        p.resistance = P(q, "resistance", 2.0);
        p.motorK = P(q, "k", 0.01);
    } else {
        p.kind = PartKind::Resistor;
        p.resistance = P(q, "resistance", 100.0);
    }
    return p;
}

} // namespace

bool IsElectrical(const std::string& type) {
    return type == "battery" || type == "wire" || type == "lamp" || type == "button" ||
           type == "switch" || type == "lever" || type == "motor" || type == "resistor" || type == "heater" || type == "relay";
}

Entity* Zone::Find(int id) {
    for (Entity& e : entities) if (e.id == id) return &e;
    return nullptr;
}

const Entity* Zone::Find(int id) const {
    for (const Entity& e : entities) if (e.id == id) return &e;
    return nullptr;
}

int Zone::NextId() const {
    int maxId = 0;
    for (const Entity& e : entities) if (!e.params.contains("_from") && !e.params.contains("spawned")) maxId = std::max(maxId, e.id);   // object parts use instance*1000+n
    int id = maxId + 1;
    while (Find(id)) ++id;
    return id;
}

void Zone::BuildCircuit(Circuit& circuit) {
    circuit = Circuit();
    for (Entity& e : entities) {
        e.part = IsElectrical(e.type) ? circuit.AddPart(MakePart(e)) : -1;
    }
    for (const TerminalLink& l : links) {
        const Entity* a = Find(l.entityA);
        const Entity* b = Find(l.entityB);
        if (!a || !b || a->part < 0 || b->part < 0) continue;
        circuit.Connect({a->part, l.termA}, {b->part, l.termB});
    }
}

namespace {
Vec3 Yaw(const Vec3& v, float deg) {
    const float a = deg * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}
bool IsVecParam(const std::string& k) { return k == "axis" || k == "dir" || k == "hinge" || k == "out"; }
}

bool ExpandPrefabs(Zone& zone, const std::string& root, std::string* log) {
    // drop anything expanded before (prefab files may have changed), keep the instances
    zone.entities.erase(std::remove_if(zone.entities.begin(), zone.entities.end(),
                                       [](const Entity& e) { return e.params.contains("_from"); }), zone.entities.end());
    bool ok = true;
    std::vector<Entity> added;
    for (const Entity& inst : zone.entities) {
        if (inst.type != "prefab") continue;
        const std::string pname = inst.params.value("prefab", std::string());
        std::ifstream f(root + "/objects/" + pname + ".json");
        json pj;
        if (!f) { if (log) *log += "missing object '" + pname + "'\n"; ok = false; continue; }
        try { f >> pj; } catch (const std::exception& ex) { if (log) *log += pname + ": " + ex.what() + "\n"; ok = false; continue; }
        const json parts = pj.value("parts", json::array());
        const json sets = inst.params.value("set", json::object());
        std::unordered_map<std::string, int> idOf;
        int k = 0;
        for (const json& pp : parts) idOf[pp.value("name", std::string())] = inst.id * 1000 + (++k);
        const float yaw = inst.rotDeg.y;
        k = 0;
        for (const json& pp : parts) {
            Entity e;
            const std::string part = pp.value("name", std::string("Part"));
            e.id = inst.id * 1000 + (++k);
            e.name = inst.name + "." + part;
            e.type = pp.value("type", std::string("static"));
            e.pos = inst.pos + Yaw(ReadV3(pp, "pos", {}), yaw);
            e.rotDeg = ReadV3(pp, "rot", {}) + Vec3{0, yaw, 0};
            e.size = ReadV3(pp, "size", {0.1f, 0.1f, 0.1f});
            if (pp.contains("color") && pp["color"].is_array()) e.color = {pp["color"][0].get<unsigned char>(), pp["color"][1].get<unsigned char>(), pp["color"][2].get<unsigned char>()};
            e.massKg = pp.value("mass", 0.f);
            e.params = pp.value("params", json::object());
            for (auto it = sets.begin(); it != sets.end(); ++it) {
                const std::string key = it.key();
                if (key.rfind(part + ".", 0) == 0) e.params[key.substr(part.size() + 1)] = it.value();
            }
            for (auto it = e.params.begin(); it != e.params.end(); ++it) {
                if (it->is_string()) {
                    std::string v = it->get<std::string>();
                    if (!v.empty() && v[0] == '$') *it = inst.name + "." + v.substr(1);   // refer to a sibling part
                    else if (v.size() > 1 && v[0] == '#' && idOf.count(v.substr(1))) *it = idOf[v.substr(1)];   // a sibling's id (valves, drains)
                } else if (IsVecParam(it.key()) && it->is_array() && it->size() == 3) {
                    const Vec3 r = Yaw({(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()}, yaw);
                    *it = json::array({r.x, r.y, r.z});
                }
            }
            e.params["_from"] = inst.name;
            added.push_back(e);
        }
        for (const json& jl : pj.value("links", json::array())) {
            const std::string a = jl.at(0).get<std::string>(), b = jl.at(2).get<std::string>();
            if (!idOf.count(a) || !idOf.count(b)) continue;
            const TerminalLink l{idOf[a], jl.at(1).get<int>(), idOf[b], jl.at(3).get<int>()};
            bool have = false;
            for (const TerminalLink& x : zone.links) have |= x.entityA == l.entityA && x.termA == l.termA && x.entityB == l.entityB && x.termB == l.termB;
            if (!have) zone.links.push_back(l);
        }
    }
    zone.entities.insert(zone.entities.end(), added.begin(), added.end());
    return ok;
}

std::vector<PrefabInfo> ListPrefabs(const std::string& root) {
    std::vector<PrefabInfo> out;
    std::error_code ec;
    for (const auto& de : std::filesystem::directory_iterator(root + "/objects", ec)) {
        if (de.path().extension() != ".json") continue;
        std::ifstream f(de.path());
        json pj;
        try { f >> pj; } catch (...) { continue; }
        out.push_back({de.path().stem().string(), pj.value("description", std::string()), static_cast<int>(pj.value("parts", json::array()).size())});
    }
    std::sort(out.begin(), out.end(), [](const PrefabInfo& a, const PrefabInfo& b) { return a.name < b.name; });
    return out;
}

bool SavePrefab(const std::string& root, const std::string& name, const std::string& description, const std::vector<Entity>& parts,
                const std::vector<TerminalLink>& links, const Vec3& origin, std::string* error) {
    json pj;
    pj["name"] = name;
    pj["description"] = description;
    pj["parts"] = json::array();
    std::unordered_map<int, std::string> nameOf;
    std::unordered_map<std::string, int> used;
    for (const Entity& e : parts) {
        std::string n = e.name.substr(e.name.find_last_of('.') == std::string::npos ? 0 : e.name.find_last_of('.') + 1);
        if (n.empty()) n = e.type;
        if (used[n]++) n += std::to_string(used[n]);
        nameOf[e.id] = n;
    }
    for (const Entity& e : parts) {
        json jp;
        jp["name"] = nameOf[e.id];
        jp["type"] = e.type;
        jp["pos"] = V3(e.pos - origin);
        jp["rot"] = V3(e.rotDeg);
        jp["size"] = V3(e.size);
        jp["color"] = json::array({e.color.r, e.color.g, e.color.b});
        jp["mass"] = e.massKg;
        json params = e.params;
        params.erase("_from");
        // ids of siblings (valve/drain) become "#Part" so they survive re-numbering
        for (auto it = params.begin(); it != params.end(); ++it)
            if ((it.key() == "valve" || it.key() == "drain") && it->is_number_integer() && nameOf.count(it->get<int>())) *it = "#" + nameOf[it->get<int>()];
        if (!params.empty()) jp["params"] = params;
        pj["parts"].push_back(jp);
    }
    pj["links"] = json::array();
    for (const TerminalLink& l : links)
        if (nameOf.count(l.entityA) && nameOf.count(l.entityB)) pj["links"].push_back(json::array({nameOf[l.entityA], l.termA, nameOf[l.entityB], l.termB}));
    std::error_code ec;
    std::filesystem::create_directories(root + "/objects", ec);
    std::ofstream f(root + "/objects/" + name + ".json");
    if (!f) { if (error) *error = "can't write " + root + "/objects/" + name + ".json"; return false; }
    f << pj.dump(2) << "\n";
    return true;
}

json ZoneToJson(const Zone& zone) {
    json j;
    j["zone"] = zone.name;
    j["ambientC"] = zone.ambientC;
    j["entities"] = json::array();
    for (const Entity& e : zone.entities) {
        if (e.params.contains("_from")) continue;   // prefab parts: re-created from the object file on load
        json je;
        je["id"] = e.id;
        je["name"] = e.name;
        je["type"] = e.type;
        je["pos"] = V3(e.pos);
        je["rot"] = V3(e.rotDeg);
        je["size"] = V3(e.size);
        je["color"] = json::array({e.color.r, e.color.g, e.color.b});
        je["mass"] = e.massKg;
        if (!e.params.empty()) je["params"] = e.params;
        j["entities"].push_back(je);
    }
    j["links"] = json::array();
    auto from = [&](int id) { const Entity* e = zone.Find(id); return e && e->params.contains("_from") ? e->params["_from"].get<std::string>() : std::string(); };
    for (const TerminalLink& l : zone.links) {
        const std::string a = from(l.entityA), b = from(l.entityB);
        if (!a.empty() && a == b) continue;   // wiring inside one object comes with the object
        j["links"].push_back(json::array({l.entityA, l.termA, l.entityB, l.termB}));
    }
    return j;
}

bool ZoneFromJson(const json& j, Zone& out, std::string* error) {
    try {
        Zone z;
        z.name = j.value("zone", std::string("Unnamed"));
        z.ambientC = j.value("ambientC", 22.f);
        for (const json& je : j.value("entities", json::array())) {
            Entity e;
            e.id = je.at("id").get<int>();
            e.name = je.value("name", std::string());
            e.type = je.value("type", std::string("static"));
            e.pos = ReadV3(je, "pos", {});
            e.rotDeg = ReadV3(je, "rot", {});
            e.size = ReadV3(je, "size", {1, 1, 1});
            if (je.contains("color") && je["color"].is_array() && je["color"].size() == 3) {
                e.color = {je["color"][0].get<unsigned char>(), je["color"][1].get<unsigned char>(), je["color"][2].get<unsigned char>()};
            }
            e.massKg = je.value("mass", 0.f);
            if (je.contains("params")) e.params = je["params"];
            z.entities.push_back(std::move(e));
        }
        for (const json& jl : j.value("links", json::array())) {
            z.links.push_back({jl.at(0).get<int>(), jl.at(1).get<int>(), jl.at(2).get<int>(), jl.at(3).get<int>()});
        }
        out = std::move(z);
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

bool LoadZone(const std::string& path, Zone& out, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    try {
        json j = json::parse(f);
        return ZoneFromJson(j, out, error);
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

bool SaveZone(const std::string& path, const Zone& zone, std::string* error) {
    std::ofstream f(path);
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    f << ZoneToJson(zone).dump(2) << '\n';
    return true;
}

} // namespace eon
