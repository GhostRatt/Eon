#pragma once
// Levels: a map made in the editor, exported for the sim with where Jenny starts.
// The start is a zone entity of type "start" (no body; its pos is where her feet go, rot.y her facing).
// Its params set up her stay: wear (comma list of garments, empty = character default), strip, memory
// (folder under memory/ that keeps what she learns between runs, empty = forgets), timeScale (game min per s).
// Exported levels live in content/levels/NAME.json: an ordinary zone plus a "level" block.
#include <string>
#include <vector>

#include "core/Zone.h"

namespace eon {

class World;

struct LevelStart {
    bool found = false;
    int entity = 0;
    Vec3 pos;              // as placed (her feet go on the floor under it)
    float yawDeg = 0.f;
    std::string wear, memory;
    bool strip = false;
    float timeScale = 1.f;
};

struct LevelInfo {
    std::string name, title, description, file;
    bool hasStart = false;
};

struct LevelIssue {
    int severity = 0;      // 0 note, 1 warning, 2 error (won't run right)
    std::string text;
};

LevelStart FindStart(const Zone& z);
// The floor under the start (ray down from just above it, else the terrain, else y as placed).
Vec3 StandingSpot(const World& w, const LevelStart& s);
// Checks a built world for problems before it's played: no start, start in a wall or over nothing,
// missing objects, script errors, dark rooms, unwired parts.
std::vector<LevelIssue> CheckLevel(World& w, const std::string& contentRoot);

struct ExportOptions {
    std::string name;          // file name (content/levels/<name>.json)
    std::string title, description;
    bool bakeObjects = false;  // expand objects into plain parts (the level no longer changes when an object file does)
};
bool ExportLevel(const std::string& contentRoot, const Zone& z, const ExportOptions& o, std::string* outPath, std::string* error);
std::vector<LevelInfo> ListLevels(const std::string& contentRoot);

} // namespace eon
