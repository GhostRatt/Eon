#include <sstream>
// eon_sim — runs Jenny's world: Waiting Area + Test Zones.
// Operator view: your mouse acts as a stand-in hand (grab/carry, push) until Jenny exists (step 4).
// Jenny never sees this HUD.

#include <cstdio>
#include <string>

#include "raylib.h"
#include "raymath.h"

#include <filesystem>
#include <fstream>
#include <memory>

#include <nlohmann/json.hpp>

#include "character/Character.h"
#include "character/BodyState.h"
#include "character/Dangles.h"
#include "mind/Learner.h"
#include "mind/Perception.h"
#include "character/Face.h"
#include "character/LipSync.h"
#include "character/Motor.h"
#include "character/Posture.h"
#include "character/Wardrobe.h"
#include "core/World.h"
#include "render/CharacterRenderer.h"
#include "render/ZoneRenderer.h"

using namespace eon;

namespace {

std::string ContentDir() {
    if (DirectoryExists("content/zones")) return "content";
    return EON_CONTENT_DIR;
}

void LoadOrWarn(World& w, const std::string& path, std::string& status) {
    std::string err;
    status = w.Load(path, &err) ? "Loaded " + w.GetZone().name : "Load failed: " + err;
}

// Jenny's body. Config: content/characters/jenny/character.json (model files are not in git).
struct Body {
    CharacterAsset asset;
    Character ch;
    CharacterRenderer gfx;
    PostureParams posture;
    BodyState state;
    BodyInputs inputs;
    FaceSim face;
    DangleSim hair;
    Wardrobe wardrobe;
    Motor motor;
    MotorIntent intent;
    LipSync lips;
    // her mind's inputs and memory (everything goes through the sensor firewall)
    Perception eyes;
    Learner mind;
    Percept percept;
    double mindT = 0.0;
    float mindAcc = 0.f, sleptGameMin = 0.f, journalAcc = 0.f;
    bool showMind = false;
    std::string memoryDir;
    std::string barefootMorph;
    bool hasWalkTarget = false;
    Vec3 walkTarget;
    float breathPhase = 0.f;
    bool loaded = false, showCapsules = false, showSkeleton = false;
    std::string info, lastWardrobeMsg;

    void Load(const std::string& content, const std::string& fbxOverride, World& world) {
        namespace fs = std::filesystem;
        const fs::path cfgPath = fs::path(content) / "characters/jenny/character.json";
        nlohmann::json cfg = nlohmann::json::object();
        if (std::ifstream f(cfgPath); f) { try { cfg = nlohmann::json::parse(f); } catch (...) {} }
        std::string fbx = fbxOverride;
        std::vector<std::string> texDirs;
        const fs::path base = cfgPath.parent_path();
        if (fbx.empty() && cfg.contains("fbx")) fbx = (base / cfg["fbx"].get<std::string>()).string();
        const nlohmann::json dirs = cfg.value("textureDirs", nlohmann::json::array());
        for (const auto& d : dirs) texDirs.push_back((base / d.get<std::string>()).string());
        texDirs.push_back((base / "source").string());
        const nlohmann::json mats = cfg.value("materials", nlohmann::json::object());
        for (const auto& [name, m] : mats.items()) {
            MaterialOverride o;
            o.baseColor = m.value("baseColor", std::string());
            o.opacity = m.value("opacity", std::string());
            o.hidden = m.value("hidden", false);
            o.skin = m.value("skin", false);
            if (m.contains("color") && m["color"].is_array() && m["color"].size() == 3) {
                o.hasColor = true;
                o.color = {m["color"][0].get<float>(), m["color"][1].get<float>(), m["color"][2].get<float>()};
            }
            gfx.overrides[name] = o;
        }
        const bool standIn = fbx.empty() || !fs::exists(fbx);
        if (standIn) {
            info = "Jenny model not found (" + (fbx.empty() ? cfgPath.string() : fbx) + "), using stand-in rig";
            fbx = std::string(EON_CONTENT_DIR) + "/../tests/data/test_rig.fbx";
        }
        ImportOptions io;
        io.targetHeight = cfg.value("targetHeight", 0.f);
        std::string err;
        if (!ImportFbx(fbx, asset, io, &err)) { info = "Import failed: " + err; return; }

        // Garments rigged to her skeleton
        if (!standIn) {
            const nlohmann::json garments = cfg.value("garments", nlohmann::json::array());
            for (const auto& gj : garments) {
                Garment g;
                g.name = gj.value("name", std::string("garment"));
                g.fbx = (base / gj.value("fbx", std::string())).string();
                const std::string layer = gj.value("layer", std::string("mid"));
                g.layer = layer == "underwear" ? Layer::Underwear : layer == "outer" ? Layer::Outer : layer == "accessory" ? Layer::Accessory : Layer::Mid;
                g.covers = gj.value("covers", std::vector<std::string>{});
                g.bodyMorphs = gj.value("bodyMorphs", std::vector<std::string>{});
                g.clo = gj.value("clo", 0.05f);
                g.worn = gj.value("worn", false);
                const nlohmann::json sup = gj.value("support", nlohmann::json::object());
                if (sup.contains("breast")) g.support[2] = sup["breast"].get<float>();
                if (sup.contains("glute")) g.support[3] = sup["glute"].get<float>();
                std::string gerr;
                if (AppendGarment(asset, g.fbx, static_cast<int>(wardrobe.garments.size()), g.meshes, &gerr))
                    wardrobe.garments.push_back(g);
                else
                    info += " [garment " + g.name + " failed: " + gerr + "]";
            }
        }

        if (!ch.Init(&asset)) { info = "Character init failed"; return; }
        const auto sp = cfg.value("spawn", std::vector<float>{0.f, 0.f, 0.3f});
        ch.placement.t = ch.placement.t + Vec3{sp[0], sp[1], sp[2]};
        ch.placement.r = Quat::AxisAngle({0, 1, 0}, cfg.value("yaw", 0.f) * 3.14159265f / 180.f);
        gfx.Init(ch, texDirs);
        face.Init(ch);
        lips.Init(ch);
        // barefoot: flatten the feet (the model's default arch is for heels) and stand her on her soles
        barefootMorph = cfg.value("barefootMorph", std::string());
        if (!barefootMorph.empty()) { ch.SetMorph(barefootMorph, 1.f); ch.RefitFloor(); }
        motor.Init(ch, world);
        hair.Init(ch, cfg.value("dangles", std::vector<std::string>{"hair_", "earring_"}));
        // memory: picks up where she left off if a memory folder is given
        if (!memoryDir.empty()) {
            fs::create_directories(memoryDir);
            mind.graph.SetJournal(memoryDir + "/jenny_memory.journal");
            mind.graph.Load(memoryDir + "/jenny_memory.json");
        }
        mind.SetBackstory(cfg.value("backstory", std::vector<std::string>{}), 0.0);
        loaded = true;
        if (info.empty()) info = fs::path(fbx).filename().string();
        info += TextFormat("  |  %d bones  %d verts  %d tris  %d tex  %d hair chains  %d garments", (int)asset.bones.size(),
                           (int)asset.VertexCount(), (int)asset.TriangleCount(), (int)gfx.LoadedTextures(),
                           (int)hair.ChainCount(), (int)wardrobe.garments.size());
    }

    void ToggleGarment(size_t i) {
        if (i >= wardrobe.garments.size()) return;
        std::string why;
        const bool ok = wardrobe.garments[i].worn ? wardrobe.Remove((int)i, &why) : wardrobe.Wear((int)i, &why);
        lastWardrobeMsg = wardrobe.garments[i].name + (ok ? (wardrobe.garments[i].worn ? " on" : " off") : ": " + why);
    }

    // Before physics: her muscles act on the world.
    void PrePhysics(World& world, float dt) {
        if (!loaded) return;
        if (hasWalkTarget) {
            Vec3 d = walkTarget - motor.Position();
            d.y = 0;
            const float dist = d.Length();
            if (dist < 0.25f) { hasWalkTarget = false; intent.moveDir = {}; }
            else intent.moveDir = d.Normalized() * std::min(1.f, dist * 0.8f);
        }
        // asleep: the body doesn't act (the mind is offline too)
        MotorIntent in = intent;
        if (state.asleep) { in = MotorIntent{}; in.stance = motor.CurrentStance(); }
        motor.PrePhysics(ch, world, in, dt);
    }

    void Update(float dt, float ambientC, World& world) {
        if (!loaded) return;
        // physiology
        inputs.ambientC = ambientC;
        inputs.clothingClo = wardrobe.TotalClo();
        inputs.exertion = posture.motion == TestMotion::Bounce ? 0.6f : posture.motion == TestMotion::WalkInPlace ? 0.35f
                                                                                                   : motor.Feedback().exertion;
        const bool still = motor.Lying() && !motor.Feedback().inTransition && motor.Feedback().speed < 0.02f &&
                           !intent.hands[0].reach && !intent.hands[1].reach;
        state.UpdateSleep(still, dt);
        inputs.sleeping = state.asleep;
        state.Step(inputs, dt);
        breathPhase = std::fmod(breathPhase + dt * state.breathRate, 1.f);

        // body
        posture.time += dt;
        posture.breathPhase = breathPhase;
        posture.breathDepth = state.breathDepth;
        posture.shiver = state.shiver;
        posture.hasLook = intent.hasLook;
        posture.lookAt = intent.lookAt;
        motor.PostPhysics(ch, world, posture, dt);
        ch.UpdateLimbCollision(dt);
        wardrobe.Apply(ch);
        ch.contactFloorY = motor.SupportY();
        ch.UpdateSoftTissue(dt);
        hair.groundY = motor.SupportY();
        hair.Update(ch, dt);

        // face
        FaceInputs fi;
        fi.mood = state.mood / 100.f;
        fi.fatigue = state.Get(Need::Fatigue) / 100.f;
        fi.cold = state.shiver;
        fi.hot = state.sweat;
        fi.breath = breathPhase;
        fi.effort = inputs.exertion;
        fi.hasLookAt = intent.hasLook;
        fi.lookAt = intent.lookAt;
        fi.asleep = inputs.sleeping ? 1.f : 0.f;
        face.Update(ch, fi, dt);
        lips.Update(ch, dt);

        ch.Skin();
        const std::vector<bool> vis = wardrobe.MeshVisibility(asset.meshes.size());
        for (size_t m = 0; m < vis.size(); ++m) gfx.SetMeshVisible(m, vis[m]);
        gfx.wetness = std::max(state.wetness, 0.f);
        gfx.Upload(ch);

        // mind: senses at 10 Hz while awake; asleep, memory consolidates (every game hour slept)
        mindT += dt;
        mindAcc += dt;
        if (mindAcc >= 0.1f) {
            mindAcc = 0.f;
            if (!state.asleep) {
                eyes.Sense(world, motor, ch, &state, mindT, percept);
                mind.Observe(percept);
            }
        }
        if (state.asleep) {
            sleptGameMin += dt * state.gameMinutesPerSecond;
            if (sleptGameMin >= 60.f) { mind.Sleep(mindT, sleptGameMin / 60.f); sleptGameMin = 0.f; }
        }
        journalAcc += dt;
        if (journalAcc > 5.f) { journalAcc = 0.f; mind.graph.FlushJournal(); }
    }

    void SaveMind(const std::string& dir) {
        namespace fs = std::filesystem;
        fs::create_directories(dir);
        if (!memoryDir.empty()) mind.graph.Save(memoryDir + "/jenny_memory.json");
        else mind.graph.Save(dir + "/jenny_memory.json");
        mind.graph.ExportHtml(dir + "/jenny_memory.html", mindT);
        std::ofstream(dir + "/jenny_memory_report.md") << mind.graph.Report(mindT);
        std::ofstream(dir + "/jenny_context.txt") << mind.Context(percept, 300);
    }

    void DrawDebug(const Camera3D& cam) const {
        if (!loaded || (!showCapsules && !showSkeleton)) return;
        BeginMode3D(cam);
        if (showCapsules) {
            for (const BodyCapsule& c : const_cast<Character&>(ch).Capsules()) {
                Vec3 a, b;
                ch.CapsuleWorld(c, a, b);
                DrawCapsuleWires(ToRl(a), ToRl(b), c.radius, 8, 4, c.tag == 2 ? PINK : c.tag == 3 ? ORANGE : SKYBLUE);
            }
        }
        if (showSkeleton) {
            const auto& w = ch.World();
            for (size_t i = 0; i < w.size(); ++i) {
                const int p = asset.bones[i].parent;
                if (p >= 0) DrawLine3D(ToRl(w[p].t), ToRl(w[i].t), YELLOW);
                DrawSphere(ToRl(w[i].t), 0.006f, RED);
            }
        }
        EndMode3D();
    }
};

} // namespace


// Scripted hand use for renders (positions match content/zones/test_zone_01.json; the tests run the same moves).
static void RunDemo(const std::string& d, float t, World& w, Body& b) {
    auto& L = b.intent.hands[0];
    auto& R = b.intent.hands[1];
    auto& in = b.intent;
    if (d == "switch") {   // stand at (2.05, 1.8) facing +X; fingertip presses the lower half of the rocker
        R.reach = t > 0.5f && t < 2.5f;
        R.shape = HandShape::Point;
        R.target = {2.449f, 1.188f, 1.8f};
        R.effort = 0.4f;
    } else if (d == "lever") {   // stand at (-1.85, 0.8) facing -X; grab the handle and throw it up
        const Vec3 pivot{-2.45f, 1.15f, 0.8f};
        const float r = 0.208f, a0 = -0.523f;
        R.reach = t > 0.3f && t < 3.6f;
        R.effort = 0.6f;
        R.grip = t > 1.3f && t < 3.2f;
        const float u = std::clamp((t - 1.5f) / 1.5f, 0.f, 1.f);
        const float a = a0 + (0.62f - a0) * u;
        R.target = t < 1.5f ? Vec3{-2.27f, 1.066f, 0.8f} : pivot + Vec3{r * std::cos(a), r * std::sin(a), 0.f};
    } else if (d == "learn") {   // stand at (0, 0.58) facing -Z: push the red button on the table twice, letting go in between
        const bool on = (t > 1.0f && t < 3.0f) || (t > 4.5f && t < 6.5f);
        R.reach = on;
        R.effort = 0.3f;
        R.target = {0.f, 0.76f, 0.15f};
    } else if (d == "crate") {   // stand at (-1.2, -1.18) facing -Z; crouch, take the crate by its sides, stand, walk
        const Vec3 c0{-1.2f, 0.15f, -1.6f};
        in.stance = t < 3.0f ? Stance::Crouch : Stance::Stand;
        for (int i = 0; i < 2; ++i) {
            auto& h = b.intent.hands[i];
            h.reach = t > 1.2f;
            h.effort = 0.7f;
            h.grip = t > 2.7f;
            h.local = t > 2.9f;
            h.target = h.local ? Vec3{i == 0 ? 0.19f : -0.19f, 0.95f, 0.3f} : c0 + Vec3{i == 0 ? -0.19f : 0.19f, 0.03f, 0.f};
        }
        in.moveDir = t > 5.0f && t < 7.0f ? Vec3{1, 0, 0} : Vec3{};
    }
    (void)L; (void)w;
}

int main(int argc, char** argv) {
    const std::string content = ContentDir();
    const std::string waiting = content + "/zones/waiting_area.json";
    const std::string test01 = content + "/zones/test_zone_01.json";

    // Args: [zone.json] [--shot out.png] [--power] [--lights-off] [--settle s] [--cam x y z tx ty tz]
    //       [--fbx model.fbx] [--motion 0-6] [--capsules] [--skeleton] [--temp C] [--timescale x] [--wear a,b] [--strip]
    std::string zonePath = waiting, shotPath, fbxOverride;
    int motionArg = 0;
    bool capsArg = false, skelArg = false, stripArg = false;
    float tempOverride = -1000.f, timeScale = 1.f;
    std::string wearArg;
    bool spawnArg = false, reachArg = false, walkArg = false;
    int stanceArg = -1;
    std::vector<std::pair<float, int>> stanceSeq;   // --stance-at T S: switch to stance S at settle time T
    bool climbArg = false;
    std::string demoArg;
    std::string memoryArg, mindOut;
    bool showMindArg = false;   // --memory DIR (persistent memory), --mind-out DIR (write report/html after settle)   // --demo switch|lever|crate|battery: scripted hand use (same moves the tests check)
    std::string sayArg;
    float spawn[3] = {0, 0, 0}, reachR[3] = {0, 0, 0}, walkTo[2] = {0, 0};
    bool powerAll = false, lightsOff = false, customCam = false;
    float settle = 0.f;
    float cam[6] = {0, 1.6f, 1.8f, 0, 0.9f, 0};
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--shot" && a + 1 < argc) shotPath = argv[++a];
        else if (arg == "--settle" && a + 1 < argc) settle = std::stof(argv[++a]);
        else if (arg == "--power") powerAll = true;
        else if (arg == "--lights-off") lightsOff = true;
        else if (arg == "--fbx" && a + 1 < argc) fbxOverride = argv[++a];
        else if (arg == "--motion" && a + 1 < argc) motionArg = std::stoi(argv[++a]);
        else if (arg == "--capsules") capsArg = true;
        else if (arg == "--skeleton") skelArg = true;
        else if (arg == "--temp" && a + 1 < argc) tempOverride = std::stof(argv[++a]);
        else if (arg == "--timescale" && a + 1 < argc) timeScale = std::stof(argv[++a]);
        else if (arg == "--wear" && a + 1 < argc) wearArg = argv[++a];
        else if (arg == "--strip") stripArg = true;
        else if (arg == "--spawn" && a + 3 < argc) { for (float& v : spawn) v = std::stof(argv[++a]); spawnArg = true; }
        else if (arg == "--reach" && a + 3 < argc) { for (float& v : reachR) v = std::stof(argv[++a]); reachArg = true; }
        else if (arg == "--walk" && a + 2 < argc) { for (float& v : walkTo) v = std::stof(argv[++a]); walkArg = true; }
        else if (arg == "--stance" && a + 1 < argc) stanceArg = std::stoi(argv[++a]);
        else if (arg == "--stance-at" && a + 2 < argc) { const float t = std::stof(argv[++a]); stanceSeq.push_back({t, std::stoi(argv[++a])}); }
        else if (arg == "--say" && a + 1 < argc) sayArg = argv[++a];
        else if (arg == "--climb") climbArg = true;
        else if (arg == "--demo" && a + 1 < argc) demoArg = argv[++a];
        else if (arg == "--memory" && a + 1 < argc) memoryArg = argv[++a];
        else if (arg == "--mind-out" && a + 1 < argc) mindOut = argv[++a];
        else if (arg == "--show-mind") showMindArg = true;
        else if (arg == "--cam" && a + 6 < argc) { for (float& c : cam) c = std::stof(argv[++a]); customCam = true; }
        else zonePath = arg;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Eon Sim");
    SetTargetFPS(120);

    ZoneRenderer renderer;
    if (!renderer.Init()) std::printf("Shader failed to compile\n");

    World world;
    std::string status;
    LoadOrWarn(world, zonePath, status);
    if (powerAll) {
        for (const Entity& e : world.GetZone().entities) {
            const Part* p = world.PartOf(e.id);
            if ((e.type == "switch" && p && !p->closed) || e.type == "button") world.DebugActuate(e.id, true);
        }
    }
    if (lightsOff) {
        for (const Entity& e : world.GetZone().entities)
            if (e.type == "switch") world.DebugActuate(e.id, false);
    }
    auto body = std::make_unique<Body>();
    body->memoryDir = memoryArg;
    body->showMind = showMindArg;
    body->Load(content, fbxOverride, world);
    body->posture.motion = static_cast<TestMotion>(std::clamp(motionArg, 0, 6));
    body->showCapsules = capsArg;
    body->showSkeleton = skelArg;
    body->state.gameMinutesPerSecond = timeScale;
    if (stripArg) for (Garment& g : body->wardrobe.garments) g.worn = false;
    for (size_t pos = 0; !wearArg.empty() && pos != std::string::npos;) {
        const size_t comma = wearArg.find(',', pos);
        const std::string name = wearArg.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const int g = body->wardrobe.Find(name);
        if (g >= 0) body->wardrobe.garments[g].worn = true;
        pos = comma == std::string::npos ? comma : comma + 1;
    }
    auto ambient = [&]() { return tempOverride > -999.f ? tempOverride : world.GetZone().ambientC; };
    if (spawnArg && body->loaded) body->motor.Teleport(body->ch, {spawn[0], 0.f, spawn[1]}, spawn[2] * DEG2RAD);
    if (reachArg) {
        auto& h = body->intent.hands[1];
        h.reach = true;
        h.target = {reachR[0], reachR[1], reachR[2]};
        h.effort = 0.3f;
        body->intent.hasLook = true;
        body->intent.lookAt = h.target;
    }
    if (walkArg) { body->walkTarget = {walkTo[0], 0.f, walkTo[1]}; body->hasWalkTarget = true; }
    if (stanceArg >= 0) body->intent.stance = static_cast<Stance>(stanceArg);
    if (!sayArg.empty()) body->lips.Say(sayArg);
    if (climbArg) { body->intent.climb = true; body->intent.moveDir = {0, 0, 1}; }
    if (settle > 0.f) {
        for (float t = 0; t < settle; t += 1 / 60.f) {
            for (const auto& [at, st] : stanceSeq) if (t >= at && t < at + 1 / 60.f) body->intent.stance = static_cast<Stance>(st);
            if (!demoArg.empty()) RunDemo(demoArg, t, world, *body);
            body->PrePhysics(world, 1 / 60.f);
            world.Advance(1 / 60.f);
            body->Update(1 / 60.f, ambient(), world);
        }
    }
    if (!mindOut.empty()) body->SaveMind(mindOut);

    FlyCamera fly;
    fly.Init({cam[0], cam[1], cam[2]}, {cam[3], cam[4], cam[5]});
    (void)customCam;

    bool showHud = true;
    float grabDist = 0.6f;
    RenderFrame frame;
    int frameCount = 0;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const bool grabbing = world.GrabbedBody() >= 0;
        fly.Update(dt, !grabbing);

        if (IsKeyPressed(KEY_ONE)) LoadOrWarn(world, waiting, status);
        if (IsKeyPressed(KEY_TWO)) LoadOrWarn(world, test01, status);
        if (IsKeyPressed(KEY_R)) { world.Reset(); status = "Reset " + world.GetZone().name; }
        if (IsKeyPressed(KEY_H)) showHud = !showHud;
        if (IsKeyPressed(KEY_M)) body->posture.motion = static_cast<TestMotion>((static_cast<int>(body->posture.motion) + 1) % 7);
        for (int k = 0; k < 6; ++k) if (IsKeyPressed(KEY_THREE + k)) body->ToggleGarment(k);   // 3..8: garments
        if (IsKeyPressed(KEY_T)) {   // cycle room temperature for testing sweat/shiver
            const float temps[] = {-1000.f, 8.f, 38.f};
            static int ti = 0;
            ti = (ti + 1) % 3;
            tempOverride = temps[ti];
        }
        if (IsKeyPressed(KEY_Y)) { timeScale = timeScale > 1.f ? 1.f : 60.f; body->state.gameMinutesPerSecond = timeScale; }
        if (IsKeyPressed(KEY_P)) body->wardrobe.streamerMode = !body->wardrobe.streamerMode;
        if (IsKeyPressed(KEY_C)) body->showCapsules = !body->showCapsules;
        if (IsKeyPressed(KEY_K)) body->showSkeleton = !body->showSkeleton;

        // Stand-in hand: LMB grab and carry, wheel = reach, F = push with a fingertip (~4 N).
        const Ray ray = GetScreenToWorldRay(GetMousePosition(), fly.cam);
        RayHit hit;
        const bool hasHit = world.RayCast(ToEon(ray.position), ToEon(ray.direction), 6.f, hit);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hasHit) {
            world.BeginGrab(hit.body, hit.point);
            grabDist = hit.dist;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) world.EndGrab();
        if (world.GrabbedBody() >= 0) {
            grabDist = Clamp(grabDist + GetMouseWheelMove() * 0.05f, 0.15f, 2.5f);
            world.SetGrabTarget(ToEon(Vector3Add(ray.position, Vector3Scale(ray.direction, grabDist))));
        }
        if (IsKeyDown(KEY_F) && hasHit) world.SetPush(hit.body, hit.point, ToEon(Vector3Scale(ray.direction, 4.f)));
        else world.ClearPush();

        // ---- Operator tests of her motor primitives (the mind will issue these later) ----
        if (hasHit && !reachArg) { body->intent.hasLook = true; body->intent.lookAt = hit.point; }
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) && hasHit) { body->walkTarget = hit.point; body->hasWalkTarget = true; }
        if (IsKeyPressed(KEY_J) && hasHit) { auto& h = body->intent.hands[1]; h.reach = true; h.target = hit.point; h.effort = 0.5f; }
        if (IsKeyPressed(KEY_U) && hasHit) { auto& h = body->intent.hands[0]; h.reach = true; h.target = hit.point; h.effort = 0.5f; }
        if (IsKeyPressed(KEY_G)) for (auto& h : body->intent.hands) if (h.reach) h.grip = !h.grip;
        if (IsKeyPressed(KEY_X)) for (auto& h : body->intent.hands) { h.reach = false; h.grip = false; }
        // stances: Z cycles stand > crouch > kneel > sit > crawl > prone > lie on back
        if (IsKeyPressed(KEY_Z)) body->intent.stance = static_cast<Stance>((static_cast<int>(body->intent.stance) + 1) % static_cast<int>(Stance::Count));
        if (IsKeyPressed(KEY_BACKSPACE)) body->intent.stance = Stance::Stand;
        // arrows: move in the direction the operator camera faces (walk, duck-walk, crawl, army crawl)
        {
            Vec3 camF = ToEon(Vector3Subtract(fly.cam.target, fly.cam.position));
            camF.y = 0;
            camF = camF.Normalized({0, 0, 1});
            const Vec3 camR = camF.Cross({0, 1, 0});
            Vec3 mv{};
            if (IsKeyDown(KEY_UP)) mv += camF;
            if (IsKeyDown(KEY_DOWN)) mv -= camF;
            if (IsKeyDown(KEY_RIGHT)) mv += camR;
            if (IsKeyDown(KEY_LEFT)) mv -= camR;
            if (mv.Length() > 0.01f) { body->intent.moveDir = mv.Normalized(); body->hasWalkTarget = false; }
            else if (!body->hasWalkTarget) body->intent.moveDir = {};
        }
        body->intent.climb = IsKeyDown(KEY_SPACE) || climbArg;
        if (IsKeyPressed(KEY_O)) body->lips.Say("Hello? Is anyone there? I don't know how I got here.");
        if (IsKeyPressed(KEY_N)) body->showMind = !body->showMind;
        if (IsKeyPressed(KEY_F9)) { body->SaveMind(memoryArg.empty() ? "mind_out" : memoryArg); status = "Jenny's memory written (html, report, context)"; }
        if (IsKeyPressed(KEY_V)) for (auto& h : body->intent.hands) if (h.reach) h.target = h.target + Vec3{0, 0.25f, 0};   // lift

        body->PrePhysics(world, dt);
        world.Tick(dt);
        world.BuildRenderFrame(frame);
        body->Update(dt, ambient(), world);

        BeginDrawing();
        ClearBackground({8, 8, 10, 255});
        renderer.Draw(frame, fly.cam, showHud && hasHit ? hit.entity : -1);
        if (body->loaded) body->gfx.Draw(frame, fly.cam, renderer.ambient);
        body->DrawDebug(fly.cam);

        if (showHud) {
            BeginMode3D(fly.cam);
            if (world.GrabbedBody() >= 0) DrawSphere(ToRl(world.GrabPoint()), 0.006f, ORANGE);
            EndMode3D();

            DrawRectangle(8, 8, 600, 176, Fade(BLACK, 0.6f));
            DrawText(TextFormat("Jenny: %s", body->info.c_str()), 16, 130, 14, PINK);
            DrawText(TextFormat("M motion [%s]  C capsules  K skeleton  T temp  Y time x%.0f  P streamer [%s]",
                                MotionName(body->posture.motion), timeScale, body->wardrobe.streamerMode ? "on" : "off"), 16, 150, 16, LIGHTGRAY);
            {
                const BodyState& st = body->state;
                std::string wr;
                for (size_t g = 0; g < body->wardrobe.garments.size() && g < 6; ++g)
                    wr += TextFormat("%d:%s%s  ", (int)g + 3, body->wardrobe.garments[g].name.c_str(), body->wardrobe.garments[g].worn ? "*" : "");
                const int x = GetScreenWidth() - 470;
                DrawRectangle(x - 8, 8, 462, 150, Fade(BLACK, 0.6f));
                DrawText(TextFormat("air %.0fC  core %.2fC  sweat %.2f  shiver %.2f", ambient(), st.coreC, st.sweat, st.shiver), x, 14, 16, RAYWHITE);
                DrawText(TextFormat("breath %.0f/min  heart %.0f bpm  mood %.0f", st.breathRate * 60.f, st.heartRate, st.mood), x, 34, 16, RAYWHITE);
                DrawText(TextFormat("tired %.0f hungry %.0f thirst %.0f bladder %.0f", st.Get(Need::Fatigue), st.Get(Need::Hunger),
                                    st.Get(Need::Thirst), st.Get(Need::Bladder)), x, 54, 16, LIGHTGRAY);
                DrawText(TextFormat("hygiene %.0f bored %.0f libido %.0f  clo %.2f", st.Get(Need::Hygiene), st.Get(Need::Boredom),
                                    st.Get(Need::Libido), body->wardrobe.TotalClo()), x, 74, 16, LIGHTGRAY);
                DrawText(wr.c_str(), x, 98, 14, PINK);
                DrawText(body->lastWardrobeMsg.c_str(), x, 118, 14, SKYBLUE);
                const MotorFeedback& mf = body->motor.Feedback();
                DrawText(TextFormat("stance %s%s%s%s  %.2f m/s%s", StanceName(mf.stance), mf.inTransition ? " (moving)" : "",
                                    mf.climbing ? " CLIMB" : "", mf.atEdge ? " EDGE" : "", mf.speed, st.asleep ? "  ASLEEP" : ""), x, 136, 14, YELLOW);
                DrawText(st.Describe(ambient()).c_str(), 16, GetScreenHeight() - 54, 16, PINK);
            }
            if (body->showMind) {
                // what her mind would be handed right now (memory context from the graph)
                const std::string ctx = body->mind.Context(body->percept, 300);
                DrawRectangle(8, 192, 760, 360, Fade(BLACK, 0.7f));
                DrawText(TextFormat("MIND  %d memories  %d links  (N hide, F9 save html/report)", (int)body->mind.graph.NodeCount(),
                                    (int)body->mind.graph.EdgeCount()), 16, 198, 16, ORANGE);
                int y = 220;
                std::istringstream ls(ctx);
                for (std::string line; std::getline(ls, line) && y < 540; y += 16) DrawText(line.substr(0, 110).c_str(), 16, y, 14, RAYWHITE);
            }
            DrawText("OPERATOR VIEW (Jenny never sees this)", 16, 14, 18, ORANGE);
            DrawText(TextFormat("%s   t = %.1fs   %d fps", world.GetZone().name.c_str(), world.SimTime(), GetFPS()), 16, 38, 18, RAYWHITE);
            DrawText("1 Waiting Area  2 Test Zone 01  R reset  H hud", 16, 62, 16, LIGHTGRAY);
            DrawText("RMB look WASD/QE fly LMB grab F push | Jenny: MMB/arrows move Z stance Space climb J/U reach G grip V lift X drop O talk N mind", 16, 82, 16, LIGHTGRAY);
            DrawText(status.c_str(), 16, 106, 16, SKYBLUE);

            if (hasHit) {
                const Entity* e = world.GetZone().Find(hit.entity);
                const Part* p = world.PartOf(hit.entity);
                std::string line = e ? e->name + " [" + e->type + "]" : "?";
                if (p) line += TextFormat("  %.2f V  %.3f A  %.2f W", p->v, p->i, p->power);
                if (p && e->type == "battery" && p->capacityAh > 0)
                    line += TextFormat("  %.0f%%", 100.0 * p->chargeAh / p->capacityAh);
                DrawText(line.c_str(), 16, GetScreenHeight() - 30, 18, YELLOW);
            }
            DrawCircleLines(GetMouseX(), GetMouseY(), 4, Fade(WHITE, 0.6f));
        }
        EndDrawing();

        if (!shotPath.empty() && ++frameCount == 10) {
            TakeScreenshot(shotPath.c_str());
            break;
        }
    }

    if (!memoryArg.empty()) body->SaveMind(memoryArg);
    renderer.Unload();
    CloseWindow();
    return 0;
}
