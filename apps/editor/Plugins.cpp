// Editor plugins: content/plugins/<name>/plugin.lua. A plugin can add parts to the content browser, settings
// (schemas) to the inspector, and tools to the Plugins menu that build things in the map. Reloaded when saved.
#include <algorithm>
#include <filesystem>
#include <fstream>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "Editor.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace eon {

namespace {
Editor* Ed(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "eon.editor");
    Editor* e = static_cast<Editor*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return e;
}

json ToJson(lua_State* L, int idx);
json ToJson(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
    case LUA_TNUMBER: return lua_isinteger(L, idx) ? json(lua_tointeger(L, idx)) : json(lua_tonumber(L, idx));
    case LUA_TSTRING: return std::string(lua_tostring(L, idx));
    case LUA_TTABLE: {
        const lua_Integer n = static_cast<lua_Integer>(lua_rawlen(L, idx));
        if (n > 0) {
            json a = json::array();
            for (lua_Integer i = 1; i <= n; ++i) { lua_rawgeti(L, idx, i); a.push_back(ToJson(L, -1)); lua_pop(L, 1); }
            return a;
        }
        json o = json::object();
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            if (lua_type(L, -2) == LUA_TSTRING) o[lua_tostring(L, -2)] = ToJson(L, -1);
            lua_pop(L, 1);
        }
        return o;
    }
    default: return nullptr;
    }
}

void PushJson(lua_State* L, const json& j) {
    if (j.is_boolean()) lua_pushboolean(L, j.get<bool>());
    else if (j.is_number_integer()) lua_pushinteger(L, j.get<long long>());
    else if (j.is_number()) lua_pushnumber(L, j.get<double>());
    else if (j.is_string()) lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_array()) { lua_createtable(L, static_cast<int>(j.size()), 0); for (size_t i = 0; i < j.size(); ++i) { PushJson(L, j[i]); lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); } }
    else if (j.is_object()) { lua_createtable(L, 0, static_cast<int>(j.size())); for (auto it = j.begin(); it != j.end(); ++it) { PushJson(L, it.value()); lua_setfield(L, -2, it.key().c_str()); } }
    else lua_pushnil(L);
}

Vec3 V(const json& j, Vec3 d) { return j.is_array() && j.size() == 3 ? Vec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()} : d; }
Rgb C(const json& j, Rgb d) { return j.is_array() && j.size() == 3 ? Rgb{j[0].get<unsigned char>(), j[1].get<unsigned char>(), j[2].get<unsigned char>()} : d; }

std::string CurrentPlugin(lua_State* L) {
    lua_getglobal(L, "PLUGIN");
    std::string p = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
    lua_pop(L, 1);
    return p;
}

void ApplyFields(Entity& e, const json& o) {
    if (o.contains("name")) e.name = o["name"].get<std::string>();
    if (o.contains("type")) e.type = o["type"].get<std::string>();
    e.pos = V(o.value("pos", json()), e.pos);
    e.rotDeg = V(o.value("rot", json()), e.rotDeg);
    e.size = V(o.value("size", json()), e.size);
    if (o.contains("mass")) e.massKg = o["mass"].get<float>();
    e.color = C(o.value("color", json()), e.color);
    if (o.contains("params") && o["params"].is_object()) for (auto it = o["params"].begin(); it != o["params"].end(); ++it) e.params[it.key()] = it.value();
}

int l_log(lua_State* L) { Ed(L)->Log("[" + CurrentPlugin(L) + "] " + luaL_checkstring(L, 1)); return 0; }

int l_register_part(lua_State* L) {
    const json o = ToJson(L, 1);
    PaletteItem it;
    it.label = o.value("label", o.value("name", std::string("part")));
    it.category = o.value("category", std::string("Plugins"));
    it.type = o.value("type", std::string("static"));
    it.size = V(o.value("size", json()), it.size);
    it.mass = o.value("mass", 0.f);
    it.color = C(o.value("color", json()), it.color);
    it.params = o.value("params", json::object());
    it.help = o.value("help", std::string());
    it.plugin = CurrentPlugin(L);
    Editor* ed = Ed(L);
    ed->palette.erase(std::remove_if(ed->palette.begin(), ed->palette.end(), [&](const PaletteItem& p) { return p.plugin == it.plugin && p.label == it.label; }), ed->palette.end());
    ed->palette.push_back(it);
    return 0;
}

int l_register_tool(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    PluginTool t;
    t.plugin = CurrentPlugin(L);
    lua_getfield(L, 1, "name");
    t.name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "tool";
    lua_pop(L, 1);
    lua_getfield(L, 1, "help");
    t.help = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    lua_getfield(L, 1, "run");
    if (!lua_isfunction(L, -1)) return luaL_error(L, "register_tool needs run = function(ctx) ... end");
    t.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    Ed(L)->pluginTools.push_back(t);
    return 0;
}

int l_register_schema(lua_State* L) {
    const std::string type = luaL_checkstring(L, 1);
    const json defs = ToJson(L, 2);
    auto& S = Ed(L)->schemas[type];
    for (const json& d : defs) {
        ParamDef p;
        p.key = d.value("key", std::string());
        if (p.key.empty()) continue;
        p.kind = d.value("kind", std::string("number"));
        p.def = d.value("default", json());
        p.lo = d.value("min", -1e6f);
        p.hi = d.value("max", 1e6f);
        p.step = d.value("step", 0.01f);
        p.help = d.value("help", std::string());
        for (const json& c : d.value("choices", json::array())) p.choices.push_back(c.get<std::string>());
        S.erase(std::remove_if(S.begin(), S.end(), [&](const ParamDef& x) { return x.key == p.key; }), S.end());
        S.push_back(p);
    }
    return 0;
}

int l_add(lua_State* L) {
    Editor* ed = Ed(L);
    const json o = ToJson(L, 1);
    Entity e;
    e.id = ed->Z().NextId();
    e.type = "static";
    e.size = {0.2f, 0.2f, 0.2f};
    ApplyFields(e, o);
    if (e.name.empty()) e.name = e.type + "_" + std::to_string(e.id);
    ed->Z().entities.push_back(e);
    lua_pushinteger(L, e.id);
    return 1;
}

int l_link(lua_State* L) {
    Ed(L)->Z().links.push_back({static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)),
                                static_cast<int>(luaL_checkinteger(L, 3)), static_cast<int>(luaL_checkinteger(L, 4))});
    return 0;
}

int l_get(lua_State* L) {
    const Entity* e = Ed(L)->Z().Find(static_cast<int>(luaL_checkinteger(L, 1)));
    if (!e) { lua_pushnil(L); return 1; }
    json o = {{"id", e->id}, {"name", e->name}, {"type", e->type}, {"pos", {e->pos.x, e->pos.y, e->pos.z}}, {"rot", {e->rotDeg.x, e->rotDeg.y, e->rotDeg.z}},
              {"size", {e->size.x, e->size.y, e->size.z}}, {"mass", e->massKg}, {"color", {e->color.r, e->color.g, e->color.b}}, {"params", e->params}};
    PushJson(L, o);
    return 1;
}

int l_set(lua_State* L) {
    Entity* e = Ed(L)->Z().Find(static_cast<int>(luaL_checkinteger(L, 1)));
    if (e) ApplyFields(*e, ToJson(L, 2));
    return 0;
}

int l_remove(lua_State* L) {
    Editor* ed = Ed(L);
    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    auto& E = ed->Z().entities;
    E.erase(std::remove_if(E.begin(), E.end(), [&](const Entity& e) { return e.id == id; }), E.end());
    auto& K = ed->Z().links;
    K.erase(std::remove_if(K.begin(), K.end(), [&](const TerminalLink& l) { return l.entityA == id || l.entityB == id; }), K.end());
    ed->selection.erase(id);
    return 0;
}

int l_selection(lua_State* L) {
    Editor* ed = Ed(L);
    lua_createtable(L, static_cast<int>(ed->selection.size()), 0);
    int i = 1;
    for (int id : ed->selection) { lua_pushinteger(L, id); lua_rawseti(L, -2, i++); }
    return 1;
}

int l_select(lua_State* L) {
    Editor* ed = Ed(L);
    ed->selection.clear();
    for (const json& j : ToJson(L, 1)) if (j.is_number_integer()) ed->selection.insert(j.get<int>());
    ed->primary = ed->selection.empty() ? -1 : *ed->selection.rbegin();
    return 0;
}

int l_cursor(lua_State* L) {
    Editor* ed = Ed(L);
    lua_pushnumber(L, ed->aim.x); lua_pushnumber(L, ed->aim.y); lua_pushnumber(L, ed->aim.z);
    return 3;
}

int l_ground(lua_State* L) {
    float y = 0.f;
    if (Ed(L)->world.GroundHeight(static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)), y)) lua_pushnumber(L, y);
    else lua_pushnil(L);
    return 1;
}

int l_find(lua_State* L) {
    const std::string n = luaL_checkstring(L, 1);
    for (const Entity& e : Ed(L)->Z().entities) if (e.name == n) { lua_pushinteger(L, e.id); return 1; }
    lua_pushnil(L);
    return 1;
}

int l_entities(lua_State* L) {
    const std::string t = luaL_optstring(L, 1, "");
    lua_newtable(L);
    int i = 1;
    for (const Entity& e : Ed(L)->Z().entities) if (t.empty() || e.type == t) { lua_pushinteger(L, e.id); lua_rawseti(L, -2, i++); }
    return 1;
}
}   // namespace

void LoadPlugins(Editor& ed) {
    ed.plugins.clear();
    ed.pluginTools.clear();
    ed.palette.erase(std::remove_if(ed.palette.begin(), ed.palette.end(), [](const PaletteItem& p) { return !p.plugin.empty(); }), ed.palette.end());
    lua_State* L = luaL_newstate();
    ed.L = L;
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1); lua_pop(L, 1);
    for (const char* bad : {"dofile", "loadfile", "require", "collectgarbage"}) { lua_pushnil(L); lua_setglobal(L, bad); }
    lua_pushlightuserdata(L, &ed);
    lua_setfield(L, LUA_REGISTRYINDEX, "eon.editor");
    const luaL_Reg api[] = {{"log", l_log}, {"register_part", l_register_part}, {"register_tool", l_register_tool}, {"register_schema", l_register_schema},
                            {"add", l_add}, {"link", l_link}, {"get", l_get}, {"set", l_set}, {"remove", l_remove}, {"selection", l_selection},
                            {"select", l_select}, {"cursor", l_cursor}, {"ground", l_ground}, {"find", l_find}, {"entities", l_entities}, {nullptr, nullptr}};
    luaL_newlib(L, api);
    lua_setglobal(L, "eon");
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "print");
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const auto& de : fs::directory_iterator(ed.root + "/plugins", ec)) if (de.is_directory() && fs::exists(de.path() / "plugin.lua")) dirs.push_back(de.path());
    std::sort(dirs.begin(), dirs.end());
    for (const fs::path& d : dirs) {
        const std::string name = d.filename().string();
        lua_pushstring(L, name.c_str());
        lua_setglobal(L, "PLUGIN");
        std::ifstream f(d / "plugin.lua");
        const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (luaL_loadbuffer(L, src.data(), src.size(), ("@" + name + "/plugin.lua").c_str()) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            ed.Log("Plugin " + name + " failed: " + lua_tostring(L, -1));
            lua_pop(L, 1);
            continue;
        }
        ed.plugins.push_back(name);
    }
    if (!dirs.empty()) ed.Log("Plugins: " + std::to_string(ed.plugins.size()) + " loaded, " + std::to_string(ed.pluginTools.size()) + " tools");
}

void UnloadPlugins(Editor& ed) {
    if (ed.L) lua_close(ed.L);
    ed.L = nullptr;
    ed.pluginTools.clear();
}

void RunPluginTool(Editor& ed, const PluginTool& t) {
    if (!ed.L || ed.playing) return;
    lua_State* L = ed.L;
    lua_pushstring(L, t.plugin.c_str());
    lua_setglobal(L, "PLUGIN");
    lua_rawgeti(L, LUA_REGISTRYINDEX, t.ref);
    json ctx = {{"cursor", {ed.aim.x, ed.aim.y, ed.aim.z}}, {"selection", json::array()}};
    for (int id : ed.selection) ctx["selection"].push_back(id);
    PushJson(L, ctx);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        ed.Log("[" + t.plugin + "] " + t.name + ": " + lua_tostring(L, -1));
        lua_pop(L, 1);
        ed.Rebuild();
        return;
    }
    ed.Commit(t.name);
}

void WatchPlugins(Editor& ed) {
    static double next = 0.0;
    if (GetTime() < next) return;
    next = GetTime() + 1.0;
    std::error_code ec;
    double newest = 0.0;
    for (auto it = fs::recursive_directory_iterator(ed.root + "/plugins", ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->path().extension() != ".lua") continue;
        newest = std::max(newest, static_cast<double>(fs::last_write_time(it->path(), ec).time_since_epoch().count()));
    }
    if (ed.pluginStamp != 0.0 && newest != ed.pluginStamp) { UnloadPlugins(ed); LoadPlugins(ed); ed.Log("Plugins reloaded"); }
    ed.pluginStamp = newest;
}

} // namespace eon
