// eon_bench — headless per-frame cost of Jenny's whole CPU pipeline (no GPU), stage by stage.
// Usage: eon_bench [zone.json] [--frames N] [--rig]   (--rig: the small test rig instead of the full model)
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "character/BodyMatter.h"
#include "character/BodyState.h"
#include "character/Character.h"
#include "character/Dangles.h"
#include "character/Face.h"
#include "character/LipSync.h"
#include "character/Motor.h"
#include "character/Posture.h"
#include "core/Jobs.h"
#include "core/World.h"
#include "mind/Learner.h"
#include "mind/Perception.h"

using namespace eon;
using Clock = std::chrono::steady_clock;

namespace {
enum Stage { Motor1, PhysicsS, Motor2, Limbs, Tissue, Hair, FaceS, Matter, Skin, Sense, Think, Render, Count };
const char* kName[] = {"motor.PrePhysics", "world.Advance", "motor.PostPhysics", "limb collision", "soft tissue", "hair",
                       "face+lips", "body matter", "skinning", "perception (10 Hz)", "learner (10 Hz)", "render frame"};
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::string zone = std::string(EON_CONTENT_DIR) + "/zones/waiting_area.json";
    int frames = 600;
    bool rig = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) frames = std::stoi(argv[++i]);
        else if (a == "--rig") rig = true;
        else zone = a;
    }
    World world;
    std::string err;
    if (!world.Load(zone, &err)) { std::printf("zone: %s\n", err.c_str()); return 1; }

    const fs::path cfgPath = fs::path(EON_CONTENT_DIR) / "characters/jenny/character.json";
    nlohmann::json cfg = nlohmann::json::object();
    if (std::ifstream f(cfgPath); f) cfg = nlohmann::json::parse(f, nullptr, false);
    std::string fbx = rig ? "" : (cfgPath.parent_path() / cfg.value("fbx", std::string())).string();
    if (fbx.empty() || !fs::exists(fbx)) fbx = std::string(EON_CONTENT_DIR) + "/../tests/data/test_rig.fbx";
    CharacterAsset asset;
    ImportOptions io;
    io.targetHeight = cfg.value("targetHeight", 0.f);
    const auto t0 = Clock::now();
    if (!ImportFbx(fbx, asset, io, &err)) { std::printf("import: %s\n", err.c_str()); return 1; }
    const double importMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    Character ch;
    ch.Init(&asset);
    FaceSim face; face.Init(ch);
    LipSync lips; lips.Init(ch);
    Motor motor; motor.Init(ch, world);
    motor.Teleport(ch, {-0.5f, 0.f, 0.5f}, 0.f);
    DangleSim hair; hair.Init(ch, cfg.value("dangles", std::vector<std::string>{"hair_", "earring_"}));
    BodyState body; BodyMatter matter; Perception eyes; Learner mind; Percept pc; RenderFrame rf;
    MotorIntent in;
    in.moveDir = {0.3f, 0.f, 0.f};   // a slow walk, so feet, balance and hair all work
    std::printf("%s: %zu bones, %zu verts, %zu tris, %zu hair chains, import %.0f ms\n", fs::path(fbx).filename().string().c_str(),
                asset.bones.size(), asset.VertexCount(), asset.TriangleCount(), hair.ChainCount(), importMs);

    {
        size_t morphs = 0, mv = 0;
        for (const SkinMesh& m : asset.meshes) { morphs += m.morphs.size(); for (const Morph& mo : m.morphs) mv += mo.index.size(); }
        std::printf("%zu meshes, %zu morph targets (%zu vertex deltas), %u worker threads\n", asset.meshes.size(), morphs, mv, WorkerCount());
    }
    double acc[Count] = {};
    const float dt = 1 / 60.f;
    float mindAcc = 0.f;
    double t = 0;
    for (int f = 0; f < frames + 60; ++f) {
        const bool count = f >= 60;   // warm-up
        auto stage = [&](Stage s, auto&& fn) { const auto a = Clock::now(); fn(); if (count) acc[s] += std::chrono::duration<double, std::milli>(Clock::now() - a).count(); };
        if (f % 240 == 120) in.moveDir = in.moveDir * -1.f;
        stage(Motor1, [&] { motor.PrePhysics(ch, world, in, dt); });
        stage(PhysicsS, [&] { world.Advance(dt); });
        PostureParams pp; pp.time = static_cast<float>(t);
        stage(Motor2, [&] { motor.PostPhysics(ch, world, pp, dt); });
        stage(Limbs, [&] { ch.UpdateLimbCollision(dt); });
        stage(Tissue, [&] { ch.contactFloorY = motor.SupportY(); ch.UpdateSoftTissue(dt); });
        stage(Hair, [&] { hair.groundY = motor.SupportY(); hair.Update(ch, dt); });
        stage(FaceS, [&] { FaceInputs fi; face.Update(ch, fi, dt); lips.Update(ch, dt); });
        stage(Matter, [&] { matter.Step(world, ch, motor, in, body, false, dt); body.Step(BodyInputs{}, dt); });
        stage(Skin, [&] { ch.Skin(); });
        mindAcc += dt;
        if (mindAcc >= 0.1f) {
            mindAcc = 0.f;
            stage(Sense, [&] { eyes.Sense(world, motor, ch, &body, t, pc); });
            stage(Think, [&] { mind.Observe(pc); });
        }
        stage(Render, [&] { world.BuildRenderFrame(rf); });
        t += dt;
    }
    double total = 0;
    for (double a : acc) total += a;
    std::printf("%-20s %8s %6s\n", "stage", "ms/frame", "share");
    for (int s = 0; s < Count; ++s) std::printf("%-20s %8.3f %5.1f%%\n", kName[s], acc[s] / frames, 100.0 * acc[s] / total);
    std::printf("%-20s %8.3f  (%.0f fps CPU budget used: %.0f%% of 16.7 ms)\n", "TOTAL", total / frames, 1000.0 / (total / frames), 100.0 * (total / frames) / 16.667);
    return 0;
}
