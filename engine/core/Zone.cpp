#include "Zone.h"

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
    } else if (e.type == "button" || e.type == "switch" || e.type == "lever") {
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
           type == "switch" || type == "lever" || type == "motor" || type == "resistor" || type == "heater";
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
    for (const Entity& e : entities) maxId = std::max(maxId, e.id);
    return maxId + 1;
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

json ZoneToJson(const Zone& zone) {
    json j;
    j["zone"] = zone.name;
    j["ambientC"] = zone.ambientC;
    j["entities"] = json::array();
    for (const Entity& e : zone.entities) {
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
    for (const TerminalLink& l : zone.links) {
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
