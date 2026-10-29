// Levels: Jenny's start point, pre-flight checks, and exporting a map for the sim.
#include "core/Level.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include "core/World.h"

namespace eon {

using nlohmann::json;
namespace fs = std::filesystem;

LevelStart FindStart(const Zone& z) {
    LevelStart s;
    for (const Entity& e : z.entities) {
        if (e.type != "start") continue;
        s.found = true;
        s.entity = e.id;
        s.pos = e.pos;
        s.yawDeg = e.rotDeg.y;
        s.wear = e.params.value("wear", std::string());
        s.memory = e.params.value("memory", std::string());
        s.strip = e.params.value("strip", false);
        s.timeScale = e.params.value("timeScale", 1.f);
        break;
    }
    return s;
}

Vec3 StandingSpot(const World& w, const LevelStart& s) {
    Vec3 p = s.pos;
    RayHit hit;
    float g = -1e9f;
    if (w.RayCast(s.pos + Vec3{0, 0.4f, 0}, {0, -1, 0}, 6.f, hit, true)) g = hit.point.y;
    float t;
    if (w.GroundHeight(p.x, p.z, t)) g = std::max(g, t);   // outdoors: never under the terrain
    if (g > -1e8f) p.y = g;
    return p;
}

std::vector<LevelIssue> CheckLevel(World& w, const std::string& root) {
    std::vector<LevelIssue> out;
    auto add = [&](int sev, const std::string& t) { out.push_back({sev, t}); };
    const Zone& z = w.GetZone();
    int starts = 0;
    for (const Entity& e : z.entities) starts += e.type == "start";
    const LevelStart s = FindStart(z);
    if (!starts) add(1, "No Jenny start: she'll appear at the middle of the map. Place one (Content > Build > Jenny start).");
    if (starts > 1) add(1, std::to_string(starts) + " Jenny starts: only the first one is used.");
    if (s.found) {
        RayHit hit;
        float g;
        const bool floor = w.RayCast(s.pos + Vec3{0, 0.4f, 0}, {0, -1, 0}, 6.f, hit, true) || w.GroundHeight(s.pos.x, s.pos.z, g);
        if (!floor) add(2, "Nothing under Jenny's start: she would fall forever.");
        const Vec3 feet = StandingSpot(w, s);
        if (feet.y < s.pos.y - 1.0f) add(1, "Jenny's start is more than 1 m above the floor under it: she'll drop.");
        if (w.RayCast(feet + Vec3{0, 0.05f, 0}, {0, 1, 0}, 1.7f, hit, true)) add(1, "Less than 1.75 m of headroom over Jenny's start (" + z.Find(hit.entity)->name + ").");
        int blocked = 0;
        for (int k = 0; k < 8; ++k) {
            const float a = k * 0.785398f;
            if (w.RayCast(feet + Vec3{0, 1.0f, 0}, {std::cos(a), 0, std::sin(a)}, 0.22f, hit, true)) ++blocked;
        }
        if (blocked >= 3) add(2, "Jenny's start is inside or against something: move it into the open.");
        else if (blocked) add(0, "Something is right next to Jenny's start.");
        if (w.WaterDepth(feet + Vec3{0, 0.9f, 0}) > 0.f) add(1, "Jenny starts in water.");
    }
    // objects that can't be found
    std::set<std::string> missing;
    for (const Entity& e : z.entities)
        if (e.type == "prefab") {
            const std::string p = e.params.value("prefab", std::string());
            if (!fs::exists(root + "/objects/" + p + ".json")) missing.insert(p);
        }
    for (const std::string& m : missing) add(2, "Missing object file: objects/" + m + ".json");
    // scripts: run the world a moment and collect what goes wrong
    w.Advance(1.0);
    int shown = 0;
    for (const std::string& l : w.ScriptLog())
        if ((l.find("rror") != std::string::npos || l.find("not found") != std::string::npos || l.find("missing") != std::string::npos) && shown++ < 6) add(1, "Script: " + l);
    // electrics nobody wired
    std::set<int> linked;
    for (const TerminalLink& l : z.links) { linked.insert(l.entityA); linked.insert(l.entityB); }
    int loose = 0, lamps = 0;
    for (const Entity& e : z.entities) {
        if (e.type == "lamp") ++lamps;
        if (e.part >= 0 && !linked.count(e.id) && e.type != "battery" && e.type != "wire" && e.type != "relay") ++loose;
    }
    if (loose) add(0, std::to_string(loose) + " electrical part(s) not wired to anything (fine if she's meant to wire them).");
    if (z.sun <= 0.f && lamps == 0) add(0, "Indoors with no lamps: the sim will be dark.");
    w.Reset();
    return out;
}

bool ExportLevel(const std::string& root, const Zone& zone, const ExportOptions& o, std::string* outPath, std::string* error) {
    if (o.name.empty() || o.name.find_first_of("/\\:*?\"<>|") != std::string::npos) { if (error) *error = "bad level name"; return false; }
    Zone z = zone;
    if (o.bakeObjects) {
        std::string log;
        ExpandPrefabs(z, root, &log);
        z.entities.erase(std::remove_if(z.entities.begin(), z.entities.end(), [](const Entity& e) { return e.type == "prefab"; }), z.entities.end());
        for (Entity& e : z.entities) e.params.erase("_from");
    }
    // things the editor or a run left behind never ship
    z.entities.erase(std::remove_if(z.entities.begin(), z.entities.end(), [](const Entity& e) { return e.params.value("spawned", false); }), z.entities.end());
    json j = ZoneToJson(z);
    json lv;
    lv["title"] = o.title.empty() ? o.name : o.title;
    lv["description"] = o.description;
    lv["baked"] = o.bakeObjects;
    lv["hasStart"] = FindStart(z).found;
    j["level"] = lv;
    std::error_code ec;
    fs::create_directories(root + "/levels", ec);
    const std::string path = root + "/levels/" + o.name + ".json";
    std::ofstream f(path);
    if (!f) { if (error) *error = "can't write " + path; return false; }
    f << j.dump(1) << "\n";
    if (outPath) *outPath = path;
    return true;
}

std::vector<LevelInfo> ListLevels(const std::string& root) {
    std::vector<LevelInfo> out;
    std::error_code ec;
    for (const auto& de : fs::directory_iterator(root + "/levels", ec)) {
        if (de.path().extension() != ".json") continue;
        std::ifstream f(de.path());
        json j;
        try { f >> j; } catch (...) { continue; }
        const json lv = j.value("level", json::object());
        LevelInfo li;
        li.name = de.path().stem().string();
        li.title = lv.value("title", li.name);
        li.description = lv.value("description", std::string());
        li.hasStart = lv.value("hasStart", false);
        li.file = de.path().string();
        out.push_back(li);
    }
    std::sort(out.begin(), out.end(), [](const LevelInfo& a, const LevelInfo& b) { return a.name < b.name; });
    return out;
}

} // namespace eon
