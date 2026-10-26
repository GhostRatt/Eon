// eon_sim — runs Jenny's world: Waiting Area + Test Zones.
// Operator view: your mouse acts as a stand-in hand (grab/carry, push) until Jenny exists (step 4).
// Jenny never sees this HUD.

#include <cstdio>
#include <string>

#include "raylib.h"
#include "raymath.h"

#include "core/World.h"
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

} // namespace

int main(int argc, char** argv) {
    const std::string content = ContentDir();
    const std::string waiting = content + "/zones/waiting_area.json";
    const std::string test01 = content + "/zones/test_zone_01.json";

    // Args: [zone.json] [--shot out.png] [--power] [--lights-off] [--settle s] [--cam x y z tx ty tz]
    std::string zonePath = waiting, shotPath;
    bool powerAll = false, lightsOff = false, customCam = false;
    float settle = 0.f;
    float cam[6] = {0, 1.6f, 1.8f, 0, 0.9f, 0};
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--shot" && a + 1 < argc) shotPath = argv[++a];
        else if (arg == "--settle" && a + 1 < argc) settle = std::stof(argv[++a]);
        else if (arg == "--power") powerAll = true;
        else if (arg == "--lights-off") lightsOff = true;
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
    if (settle > 0.f) world.Advance(settle);

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

        BeginDrawing();
        ClearBackground({8, 8, 10, 255});
        renderer.Draw(frame, fly.cam, showHud && hasHit ? hit.entity : -1);

        if (showHud) {
            BeginMode3D(fly.cam);
            if (world.GrabbedBody() >= 0) DrawSphere(ToRl(world.GrabPoint()), 0.006f, ORANGE);
            EndMode3D();

            DrawRectangle(8, 8, 600, 128, Fade(BLACK, 0.6f));
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
