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
#include "character/Posture.h"
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
    bool loaded = false, showCapsules = false, showSkeleton = false, supported = false;
    std::string info;

    void Load(const std::string& content, const std::string& fbxOverride) {
        namespace fs = std::filesystem;
        const fs::path cfgPath = fs::path(content) / "characters/jenny/character.json";
        nlohmann::json cfg = nlohmann::json::object();
        if (std::ifstream f(cfgPath); f) { try { cfg = nlohmann::json::parse(f); } catch (...) {} }
        std::string fbx = fbxOverride;
        std::vector<std::string> texDirs;
        const fs::path base = cfgPath.parent_path();
        if (fbx.empty() && cfg.contains("fbx")) fbx = (base / cfg["fbx"].get<std::string>()).string();
        for (const auto& d : cfg.value("textureDirs", nlohmann::json::array())) texDirs.push_back((base / d.get<std::string>()).string());
        texDirs.push_back((base / "source").string());
        if (fbx.empty() || !fs::exists(fbx)) {
            info = "Jenny model not found (" + (fbx.empty() ? cfgPath.string() : fbx) + "), using stand-in rig";
            fbx = std::string(EON_CONTENT_DIR) + "/../tests/data/test_rig.fbx";
        }
        ImportOptions io;
        io.targetHeight = cfg.value("targetHeight", 0.f);
        std::string err;
        if (!ImportFbx(fbx, asset, io, &err)) { info = "Import failed: " + err; return; }
        if (!ch.Init(&asset)) { info = "Character init failed"; return; }
        const auto sp = cfg.value("spawn", std::vector<float>{0.f, 0.f, 0.3f});
        ch.placement.t = ch.placement.t + Vec3{sp[0], sp[1], sp[2]};
        ch.placement.r = Quat::AxisAngle({0, 1, 0}, cfg.value("yaw", 0.f) * 3.14159265f / 180.f);
        gfx.Init(ch, texDirs);
        loaded = true;
        if (info.empty()) info = fs::path(fbx).filename().string();
        info += TextFormat("  |  %d bones  %d verts  %d tris  %d tex", (int)asset.bones.size(), (int)asset.VertexCount(),
                           (int)asset.TriangleCount(), (int)gfx.LoadedTextures());
    }

    void Update(float dt) {
        if (!loaded) return;
        posture.time += dt;
        ApplyPosture(ch, posture);
        ch.UpdateLimbCollision(dt);
        ch.SetSupport(2, supported ? 0.7f : 0.f);
        ch.UpdateSoftTissue(dt);
        ch.Skin();
        gfx.Upload(ch);
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

int main(int argc, char** argv) {
    const std::string content = ContentDir();
    const std::string waiting = content + "/zones/waiting_area.json";
    const std::string test01 = content + "/zones/test_zone_01.json";

    // Args: [zone.json] [--shot out.png] [--power] [--lights-off] [--settle s] [--cam x y z tx ty tz]
    //       [--fbx model.fbx] [--motion 0-6] [--capsules] [--skeleton]
    std::string zonePath = waiting, shotPath, fbxOverride;
    int motionArg = 0;
    bool capsArg = false, skelArg = false;
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
    body->Load(content, fbxOverride);
    body->posture.motion = static_cast<TestMotion>(std::clamp(motionArg, 0, 6));
    body->showCapsules = capsArg;
    body->showSkeleton = skelArg;
    if (settle > 0.f) {
        world.Advance(settle);
        for (float t = 0; t < settle; t += 1 / 60.f) body->Update(1 / 60.f);
    }

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
        if (IsKeyPressed(KEY_B)) body->supported = !body->supported;
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

        world.Tick(dt);
        world.BuildRenderFrame(frame);
        body->Update(dt);

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
            DrawText(TextFormat("M motion [%s]  B support [%s]  C capsules  K skeleton", MotionName(body->posture.motion),
                                body->supported ? "on" : "off"), 16, 150, 16, LIGHTGRAY);
            DrawText("OPERATOR VIEW (Jenny never sees this)", 16, 14, 18, ORANGE);
            DrawText(TextFormat("%s   t = %.1fs   %d fps", world.GetZone().name.c_str(), world.SimTime(), GetFPS()), 16, 38, 18, RAYWHITE);
            DrawText("1 Waiting Area  2 Test Zone 01  R reset  H hud", 16, 62, 16, LIGHTGRAY);
            DrawText("RMB look  WASD/QE fly  LMB grab+carry (wheel reach)  F push", 16, 82, 16, LIGHTGRAY);
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

    renderer.Unload();
    CloseWindow();
    return 0;
}
