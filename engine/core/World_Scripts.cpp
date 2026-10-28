// Object scripts: Lua 5.4. Each scripted entity runs in its own sandboxed environment with `self` (its own
// entity), `world` (find/spawn/time/sensors), and timers (`after`, `every`). Callbacks (all optional):
//   on_start(self)            once, when the zone starts
//   on_tick(self, dt)         30 times a second
//   on_press(self) / on_release(self)   when the object's own button/switch/lever/relay closes or opens
// Errors are caught: the script is switched off and the message goes to the operator log.
#include "World.h"

#include <algorithm>
#include <fstream>
#include <sstream>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace eon {

namespace {

const char* kWorldKey = "eon.world";
const char* kEntityMeta = "eon.entity";

World* W(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kWorldKey);
    World* w = static_cast<World*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return w;
}

void PushEntity(lua_State* L, int id) {
    lua_newtable(L);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "id");
    luaL_setmetatable(L, kEntityMeta);
}

int IdOf(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TTABLE);
    lua_getfield(L, idx, "id");
    const int id = static_cast<int>(luaL_checkinteger(L, -1));
    lua_pop(L, 1);
    return id;
}

const Entity* Ent(lua_State* L, int idx) {
    const Entity* e = W(L)->GetZone().Find(IdOf(L, idx));
    if (!e) luaL_error(L, "that object no longer exists");
    return e;
}

void PushJson(lua_State* L, const nlohmann::json& j) {
    if (j.is_boolean()) lua_pushboolean(L, j.get<bool>());
    else if (j.is_number_integer()) lua_pushinteger(L, j.get<long long>());
    else if (j.is_number()) lua_pushnumber(L, j.get<double>());
    else if (j.is_string()) lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_array()) {
        lua_newtable(L);
        int k = 1;
        for (const auto& x : j) { PushJson(L, x); lua_rawseti(L, -2, k++); }
    } else if (j.is_object()) {
        lua_newtable(L);
        for (auto it = j.begin(); it != j.end(); ++it) { PushJson(L, it.value()); lua_setfield(L, -2, it.key().c_str()); }
    } else lua_pushnil(L);
}

nlohmann::json ToJson(lua_State* L, int idx) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
    case LUA_TNUMBER: return lua_isinteger(L, idx) ? nlohmann::json(lua_tointeger(L, idx)) : nlohmann::json(lua_tonumber(L, idx));
    case LUA_TSTRING: return std::string(lua_tostring(L, idx));
    case LUA_TTABLE: {
        // array if it has [1], else object
        lua_rawgeti(L, idx, 1);
        const bool arr = !lua_isnil(L, -1);
        lua_pop(L, 1);
        nlohmann::json j = arr ? nlohmann::json::array() : nlohmann::json::object();
        if (arr) {
            for (int k = 1;; ++k) { lua_rawgeti(L, idx, k); if (lua_isnil(L, -1)) { lua_pop(L, 1); break; } j.push_back(ToJson(L, -1)); lua_pop(L, 1); }
        } else {
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                if (lua_type(L, -2) == LUA_TSTRING) j[lua_tostring(L, -2)] = ToJson(L, -1);
                lua_pop(L, 1);
            }
        }
        return j;
    }
    default: return nullptr;
    }
}

Vec3 V3(lua_State* L, int i) {
    return {static_cast<float>(luaL_checknumber(L, i)), static_cast<float>(luaL_checknumber(L, i + 1)), static_cast<float>(luaL_checknumber(L, i + 2))};
}

// ---- entity methods ----
int e_name(lua_State* L) { lua_pushstring(L, Ent(L, 1)->name.c_str()); return 1; }
int e_type(lua_State* L) { lua_pushstring(L, Ent(L, 1)->type.c_str()); return 1; }
int e_pos(lua_State* L) {
    World* w = W(L);
    const int b = w->MainBodyOf(IdOf(L, 1));
    const Vec3 p = b >= 0 ? w->BodyPosition(b) : Ent(L, 1)->pos;
    lua_pushnumber(L, p.x); lua_pushnumber(L, p.y); lua_pushnumber(L, p.z);
    return 3;
}
int e_vel(lua_State* L) {
    World* w = W(L);
    const Vec3 v = w->BodyVelocity(w->MainBodyOf(IdOf(L, 1)));
    lua_pushnumber(L, v.x); lua_pushnumber(L, v.y); lua_pushnumber(L, v.z);
    return 3;
}
int e_param(lua_State* L) {
    const Entity* e = Ent(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (e->params.contains(k)) PushJson(L, e->params[k]);
    else if (lua_gettop(L) >= 3) lua_pushvalue(L, 3);
    else lua_pushnil(L);
    return 1;
}
int e_set_param(lua_State* L) {
    Entity* e = W(L)->GetZone().Find(IdOf(L, 1));
    if (!e) return luaL_error(L, "that object no longer exists");
    e->params[luaL_checkstring(L, 2)] = ToJson(L, 3);
    return 0;
}
int e_closed(lua_State* L) { const Part* p = W(L)->PartOf(IdOf(L, 1)); lua_pushboolean(L, p && p->closed); return 1; }
int e_power(lua_State* L) { const Part* p = W(L)->PartOf(IdOf(L, 1)); lua_pushnumber(L, p ? p->power : 0.0); return 1; }
int e_current(lua_State* L) { const Part* p = W(L)->PartOf(IdOf(L, 1)); lua_pushnumber(L, p ? p->i : 0.0); return 1; }
int e_voltage(lua_State* L) { const Part* p = W(L)->PartOf(IdOf(L, 1)); lua_pushnumber(L, p ? p->v : 0.0); return 1; }
int e_charge(lua_State* L) { const Part* p = W(L)->PartOf(IdOf(L, 1)); lua_pushnumber(L, p && p->capacityAh > 0 ? p->chargeAh / p->capacityAh : 0.0); return 1; }
int e_set_relay(lua_State* L) { W(L)->SetRelay(IdOf(L, 1), lua_toboolean(L, 2) != 0); return 0; }
int e_set_tap(lua_State* L) { W(L)->SetTapScriptOpen(IdOf(L, 1), lua_toboolean(L, 2) != 0); return 0; }
int e_set_glow(lua_State* L) { W(L)->SetEmissive(IdOf(L, 1), static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int e_set_color(lua_State* L) {
    auto c = [&](int i) { return static_cast<unsigned char>(std::clamp<lua_Integer>(luaL_checkinteger(L, i), 0, 255)); };
    W(L)->SetColor(IdOf(L, 1), Rgb{c(2), c(3), c(4)});
    return 0;
}
int e_push(lua_State* L) {
    World* w = W(L);
    const int b = w->MainBodyOf(IdOf(L, 1));
    if (b >= 0) w->SetPush(b, w->BodyPosition(b), V3(L, 2), 0.05f);
    return 0;
}
int e_angle(lua_State* L) { lua_pushnumber(L, W(L)->DoorAngle(IdOf(L, 1))); return 1; }
int e_shut(lua_State* L) { lua_pushboolean(L, W(L)->DoorShut(IdOf(L, 1))); return 1; }
int e_set_locked(lua_State* L) { W(L)->SetDoorLocked(IdOf(L, 1), lua_toboolean(L, 2) != 0); return 0; }
int e_locked(lua_State* L) { lua_pushboolean(L, W(L)->DoorLocked(IdOf(L, 1))); return 1; }
int e_water(lua_State* L) { lua_pushnumber(L, W(L)->ContainerMl(IdOf(L, 1))); return 1; }
int e_food_left(lua_State* L) { lua_pushnumber(L, W(L)->FoodLeftG(IdOf(L, 1))); return 1; }

// ---- world functions ----
int w_find(lua_State* L) {
    const char* n = luaL_checkstring(L, 1);
    for (const Entity& e : W(L)->GetZone().entities)
        if (e.name == n) { PushEntity(L, e.id); return 1; }
    lua_pushnil(L);
    return 1;
}
int w_time(lua_State* L) { lua_pushnumber(L, W(L)->SimTime()); return 1; }
int w_air_temp(lua_State* L) { lua_pushnumber(L, W(L)->AirTemperature(V3(L, 1))); return 1; }
int w_agent_near(lua_State* L) { lua_pushboolean(L, W(L)->AgentNear(V3(L, 1), static_cast<float>(luaL_checknumber(L, 4)))); return 1; }
int w_log(lua_State* L) {
    std::string s;
    for (int i = 1; i <= lua_gettop(L); ++i) { if (i > 1) s += " "; s += luaL_tolstring(L, i, nullptr); lua_pop(L, 1); }
    W(L)->ScriptLogLine(s);
    return 0;
}
int w_spawn_object(lua_State* L) {
    // world.spawn_object(name, x, y, z [, yawDeg [, {["Part.key"]=value}]]) -> name of the new object or nil
    const std::string obj = luaL_checkstring(L, 1);
    const Vec3 p = V3(L, 2);
    const float yaw = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    const nlohmann::json set = lua_istable(L, 6) ? ToJson(L, 6) : nlohmann::json::object();
    const std::string n = W(L)->SpawnObject(obj, p, yaw, set);
    if (n.empty()) lua_pushnil(L); else lua_pushstring(L, n.c_str());
    return 1;
}

int w_spawn(lua_State* L) {
    // world.spawn(type, x, y, z, {name=, size={x,y,z}, mass=, color={r,g,b}, params={...}})
    Entity e;
    e.type = luaL_checkstring(L, 1);
    e.pos = V3(L, 2);
    e.size = {0.1f, 0.1f, 0.1f};
    e.massKg = 0.2f;
    e.name = e.type;
    if (lua_istable(L, 5)) {
        const nlohmann::json o = ToJson(L, 5);
        if (o.contains("name")) e.name = o["name"].get<std::string>();
        if (o.contains("size") && o["size"].is_array()) e.size = {o["size"][0].get<float>(), o["size"][1].get<float>(), o["size"][2].get<float>()};
        if (o.contains("mass")) e.massKg = o["mass"].get<float>();
        if (o.contains("color") && o["color"].is_array())
            e.color = {static_cast<unsigned char>(o["color"][0].get<int>()), static_cast<unsigned char>(o["color"][1].get<int>()), static_cast<unsigned char>(o["color"][2].get<int>())};
        if (o.contains("params")) e.params = o["params"];
    }
    const int id = W(L)->Spawn(e);
    if (id > 0) PushEntity(L, id); else lua_pushnil(L);
    return 1;
}

const char* kPrelude = R"LUA(
local __timers = {}
function after(t, fn) __timers[#__timers + 1] = {at = world.time() + t, fn = fn} end
function every(t, fn) __timers[#__timers + 1] = {at = world.time() + t, fn = fn, every = t} end
function __eon_timers()
  local now = world.time()
  local i = 1
  while i <= #__timers do
    local tm = __timers[i]
    if now >= tm.at then
      tm.fn(self)
      if tm.every then tm.at = tm.at + tm.every; i = i + 1 else table.remove(__timers, i) end
    else i = i + 1 end
  end
end
)LUA";

} // namespace

void World::ScriptLogLine(const std::string& s) {
    scriptLog_.push_back(s);
    if (scriptLog_.size() > 200) scriptLog_.erase(scriptLog_.begin());
}

bool World::AgentNear(const Vec3& p, float r) const {
    for (size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i].role == Role::Agent && Distance(BodyPosition(static_cast<int>(i)), p) < r) return true;
    return false;
}

void World::SetRelay(int id, bool closed) {
    const Entity* e = zone_.Find(id);
    if (e && e->part >= 0 && e->type == "relay") circuit_.GetPart(e->part).closed = closed;
}

void World::SetTapScriptOpen(int id, bool open) {
    for (TapRt& t : taps_) if (t.entity == id) t.scriptOpen = open;
}

void World::SetEmissive(int id, float e) { emissiveOverride_[id] = e; }
void World::SetColor(int id, Rgb c) { colorOverride_[id] = c; }

void World::Despawn(int id) {
    for (RtBody& b : bodies_)
        if (b.entity == id && b.h != kNoBody) { physics_->RemoveBody(b.h); b.h = kNoBody; b.entity = 0; }
    mainBody_.erase(id);
    for (FoodRt& f : foods_) if (f.entity == id) f.gone = true;
    scripts_.erase(std::remove_if(scripts_.begin(), scripts_.end(), [id](const ScriptRt& s) { return s.entity == id; }), scripts_.end());
    zone_.links.erase(std::remove_if(zone_.links.begin(), zone_.links.end(), [id](const TerminalLink& l) { return l.entityA == id || l.entityB == id; }), zone_.links.end());
    zone_.entities.erase(std::remove_if(zone_.entities.begin(), zone_.entities.end(), [id](const Entity& e) { return e.id == id; }), zone_.entities.end());
}

int World::Spawn(Entity e) {
    if (e.type == "wire" && !e.params.value("hidden", false)) { ScriptLogLine("spawn: loose wires can't be added while running"); return 0; }
    if (e.id <= 0 || zone_.Find(e.id)) e.id = zone_.NextId();
    e.params["spawned"] = true;
    e.part = IsElectrical(e.type) ? circuit_.AddPart(MakePart(e)) : -1;   // the solver rebuilds its nodes every step
    zone_.entities.push_back(e);
    BuildEntity(zone_.entities.back());
    if (L_ && (e.params.contains("script") || e.params.contains("script_src"))) {
        ScriptRt s;
        s.entity = e.id;
        if (LoadScript(s)) scripts_.push_back(s);
    }
    return e.id;
}

std::string World::SpawnObject(const std::string& object, const Vec3& pos, float yawDeg, const nlohmann::json& set) {
    Zone tmp;
    Entity inst;
    inst.id = zone_.NextId();
    inst.name = object + "_" + std::to_string(inst.id);
    inst.type = "prefab";
    inst.pos = pos;
    inst.rotDeg = {0, yawDeg, 0};
    inst.size = {0.1f, 0.1f, 0.1f};
    inst.params = {{"prefab", object}, {"set", set}, {"spawned", true}};
    tmp.entities.push_back(inst);
    std::string log;
    if (!ExpandPrefabs(tmp, contentRoot_.empty() ? "content" : contentRoot_, &log)) { ScriptLogLine("spawn_object: " + log); return {}; }
    zone_.entities.push_back(inst);
    bool scripted = false;
    for (const Entity& e : tmp.entities) {
        if (e.type == "prefab") continue;
        scripted |= e.params.contains("script") || e.params.contains("script_src");
        if (!L_) { Entity c = e; c.params["spawned"] = true; c.part = IsElectrical(c.type) ? circuit_.AddPart(MakePart(c)) : -1; zone_.entities.push_back(c); BuildEntity(zone_.entities.back()); }
        else Spawn(e);   // keeps the part's id, starts its script
    }
    for (const TerminalLink& l : tmp.links) {
        zone_.links.push_back(l);
        const Entity* a = zone_.Find(l.entityA);
        const Entity* b = zone_.Find(l.entityB);
        if (a && b && a->part >= 0 && b->part >= 0) idealLinks_.push_back({a->part, l.termA, b->part, l.termB});
    }
    if (!L_ && scripted) StartScripts();   // first scripted thing in the world
    UpdateSwitchStates();
    UpdateConnections();
    return inst.name;
}

void World::StopScripts() {
    if (L_) lua_close(L_);
    L_ = nullptr;
    scripts_.clear();
    emissiveOverride_.clear();
    colorOverride_.clear();
}

bool World::LoadScript(ScriptRt& s) {
    const Entity* e = zone_.Find(s.entity);
    if (!e) return false;
    if (e->params.contains("script_src")) {
        s.source = e->params["script_src"].get<std::string>();
        s.file = e->name + " (inline)";
    } else {
        s.file = e->params["script"].get<std::string>();
        std::ifstream f(contentRoot_ + "/" + s.file);
        if (!f) { ScriptLogLine(e->name + ": can't open script " + contentRoot_ + "/" + s.file); return false; }
        std::stringstream ss;
        ss << f.rdbuf();
        s.source = ss.str();
    }
    // a fresh environment for this object: safe globals, self, world, timers
    lua_newtable(L_);
    lua_newtable(L_);
    lua_pushglobaltable(L_);
    lua_setfield(L_, -2, "__index");
    lua_setmetatable(L_, -2);
    PushEntity(L_, s.entity);
    lua_setfield(L_, -2, "self");
    const int env = lua_gettop(L_);
    auto run = [&](const std::string& src, const std::string& name) {
        if (luaL_loadbuffer(L_, src.data(), src.size(), name.c_str()) != LUA_OK) return false;
        lua_pushvalue(L_, env);
        lua_setupvalue(L_, -2, 1);   // _ENV
        return lua_pcall(L_, 0, 0, 0) == LUA_OK;
    };
    if (!run(kPrelude, "prelude") || !run(s.source, "@" + s.file)) {
        ScriptLogLine(e->name + ": " + lua_tostring(L_, -1));
        lua_settop(L_, env - 1);
        s.broken = true;
        return false;
    }
    s.env = luaL_ref(L_, LUA_REGISTRYINDEX);
    // on_start
    lua_rawgeti(L_, LUA_REGISTRYINDEX, s.env);
    lua_getfield(L_, -1, "on_start");
    if (lua_isfunction(L_, -1)) {
        PushEntity(L_, s.entity);
        if (lua_pcall(L_, 1, 0, 0) != LUA_OK) { ScriptLogLine(e->name + ": " + lua_tostring(L_, -1)); lua_pop(L_, 1); s.broken = true; }
    } else lua_pop(L_, 1);
    lua_pop(L_, 1);
    const Part* p = PartOf(s.entity);
    s.lastClosed = p && p->closed;
    return true;
}

void World::StartScripts() {
    StopScripts();
    bool any = false;
    for (const Entity& e : zone_.entities) any |= e.params.contains("script") || e.params.contains("script_src");
    if (!any) return;
    L_ = luaL_newstate();
    // safe library set: no io, no os (except clock/time), no loading files from disk
    luaL_requiref(L_, "_G", luaopen_base, 1); lua_pop(L_, 1);
    luaL_requiref(L_, "table", luaopen_table, 1); lua_pop(L_, 1);
    luaL_requiref(L_, "string", luaopen_string, 1); lua_pop(L_, 1);
    luaL_requiref(L_, "math", luaopen_math, 1); lua_pop(L_, 1);
    luaL_requiref(L_, "utf8", luaopen_utf8, 1); lua_pop(L_, 1);
    luaL_requiref(L_, "os", luaopen_os, 1);
    lua_newtable(L_);
    lua_getfield(L_, -2, "clock"); lua_setfield(L_, -2, "clock");
    lua_getfield(L_, -2, "time"); lua_setfield(L_, -2, "time");
    lua_setglobal(L_, "os");
    lua_pop(L_, 1);
    for (const char* bad : {"dofile", "loadfile", "require", "collectgarbage"}) { lua_pushnil(L_); lua_setglobal(L_, bad); }
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, kWorldKey);
    // entity metatable
    luaL_newmetatable(L_, kEntityMeta);
    const luaL_Reg methods[] = {{"name", e_name}, {"type", e_type}, {"pos", e_pos}, {"vel", e_vel}, {"param", e_param}, {"set_param", e_set_param},
                                {"closed", e_closed}, {"angle", e_angle}, {"shut", e_shut}, {"set_locked", e_set_locked}, {"locked", e_locked}, {"power", e_power}, {"current", e_current}, {"voltage", e_voltage}, {"charge", e_charge},
                                {"set_relay", e_set_relay}, {"set_tap", e_set_tap}, {"set_glow", e_set_glow}, {"set_color", e_set_color},
                                {"push", e_push}, {"water", e_water}, {"food_left", e_food_left}, {nullptr, nullptr}};
    lua_newtable(L_);
    luaL_setfuncs(L_, methods, 0);
    lua_setfield(L_, -2, "__index");
    lua_pop(L_, 1);
    // world table
    const luaL_Reg wf[] = {{"find", w_find}, {"time", w_time}, {"air_temp", w_air_temp}, {"agent_near", w_agent_near},
                           {"log", w_log}, {"spawn", w_spawn}, {"spawn_object", w_spawn_object}, {nullptr, nullptr}};
    lua_newtable(L_);
    luaL_setfuncs(L_, wf, 0);
    lua_setglobal(L_, "world");
    lua_pushcfunction(L_, w_log);
    lua_setglobal(L_, "print");   // print goes to the operator log
    for (const Entity& e : zone_.entities) {
        if (!(e.params.contains("script") || e.params.contains("script_src"))) continue;
        ScriptRt s;
        s.entity = e.id;
        if (LoadScript(s)) scripts_.push_back(s);
    }
}

void World::ReloadScripts() {
    emissiveOverride_.clear();
    colorOverride_.clear();
    StartScripts();
    ScriptLogLine("scripts reloaded (" + std::to_string(scripts_.size()) + ")");
}

void World::StepScripts(float dt) {
    if (!L_ || scripts_.empty()) return;
    scriptAcc_ += dt;
    if (scriptAcc_ < 1.f / 30.f) return;
    const float step = scriptAcc_;
    scriptAcc_ = 0.f;
    // careful: a callback can spawn objects with scripts, which grows scripts_ (no references held across calls)
    for (size_t i = 0; i < scripts_.size(); ++i) {
        if (scripts_[i].broken) continue;
        const int entity = scripts_[i].entity, env = scripts_[i].env;
        const Entity* e = zone_.Find(entity);
        if (!e) { scripts_[i].broken = true; continue; }
        const std::string name = e->name;
        auto call = [&](const char* fn, bool withDt) {
            lua_rawgeti(L_, LUA_REGISTRYINDEX, env);
            lua_getfield(L_, -1, fn);
            if (!lua_isfunction(L_, -1)) { lua_pop(L_, 2); return; }
            PushEntity(L_, entity);
            int n = 1;
            if (withDt) { lua_pushnumber(L_, step); ++n; }
            if (lua_pcall(L_, n, 0, 0) != LUA_OK) {
                ScriptLogLine(name + ": " + lua_tostring(L_, -1) + " (script stopped)");
                lua_pop(L_, 1);
                scripts_[i].broken = true;
            }
            lua_pop(L_, 1);
        };
        const Part* p = PartOf(entity);
        const bool closed = p && p->closed;
        if (closed != scripts_[i].lastClosed) { scripts_[i].lastClosed = closed; call(closed ? "on_press" : "on_release", false); }
        if (!scripts_[i].broken) call("__eon_timers", false);
        if (!scripts_[i].broken) call("on_tick", true);
    }
}

} // namespace eon
