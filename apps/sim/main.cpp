// eon_sim — runs Jenny's world: Waiting Area + Test Zones.
// Operator controls only (Jenny is added in step 4 and never sees this HUD).

#include <cstdio>
#include <string>

#include "raylib.h"

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

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Eon Sim");
    SetTargetFPS(120);

    ZoneRenderer renderer;
    if (!renderer.Init()) std::printf("Shader failed to compile\n");

    // Args: [zone.json] [--shot out.png] [--power]   (--power flips switches/holds buttons, for checks)
    std::string zonePath = waiting, shotPath;
    bool powerAll = false;
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--shot" && a + 1 < argc) shotPath = argv[++a];
        else if (arg == "--power") powerAll = true;
        else zonePath = arg;
    }

    World world;
    std::string status;
    LoadOrWarn(world, zonePath, status);
    if (powerAll) {
        for (const Entity& e : world.GetZone().entities) {
            const Part* p = world.PartOf(e.id);
            if (e.type == "switch" && p && !p->closed) world.Toggle(e.id);
            if (e.type == "button") world.SetPressed(e.id, true);
        }
    }
    int frame = 0;

    FlyCamera fly;
    fly.Init({0, 1.6f, 2.5f}, {0, 1.2f, 0});

    bool showHud = true;
    int heldButton = -1;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        fly.Update(dt);

        if (IsKeyPressed(KEY_ONE)) LoadOrWarn(world, waiting, status);
        if (IsKeyPressed(KEY_TWO)) LoadOrWarn(world, test01, status);
        if (IsKeyPressed(KEY_R)) { world.Reset(); status = "Reset " + world.GetZone().name; }
        if (IsKeyPressed(KEY_H)) showHud = !showHud;

        // Operator debug interaction: hold LMB on a button, click a switch.
        const Ray ray = GetScreenToWorldRay(GetMousePosition(), fly.cam);
        const int hover = ZoneRenderer::Pick(world, ray);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover > 0) {
            const Entity* e = world.GetZone().Find(hover);
            if (e && e->type == "button") { world.SetPressed(hover, true); heldButton = hover; }
            if (e && e->type == "switch") world.Toggle(hover);
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && heldButton > 0) {
            world.SetPressed(heldButton, false);
            heldButton = -1;
        }

        world.Tick(dt);

        BeginDrawing();
        ClearBackground({8, 8, 10, 255});
        renderer.Draw(world, fly.cam, showHud ? hover : -1);

        if (showHud) {
            DrawRectangle(8, 8, 520, 128, Fade(BLACK, 0.6f));
            DrawText("OPERATOR VIEW (Jenny never sees this)", 16, 14, 18, ORANGE);
            DrawText(TextFormat("%s   t = %.1fs   %d fps", world.GetZone().name.c_str(), world.SimTime(), GetFPS()), 16, 38, 18, RAYWHITE);
            DrawText("1 Waiting Area  2 Test Zone 01  R reset  H hud", 16, 62, 16, LIGHTGRAY);
            DrawText("RMB look  WASD move  Q/E down/up  LMB press button / flip switch", 16, 82, 16, LIGHTGRAY);
            DrawText(status.c_str(), 16, 106, 16, SKYBLUE);

            if (hover > 0) {
                const Entity* e = world.GetZone().Find(hover);
                const Part* p = world.PartOf(hover);
                std::string line = e->name + " [" + e->type + "]";
                if (p) line += TextFormat("  %.2f V  %.3f A  %.2f W", p->v, p->i, p->power);
                if (p && e->type == "battery" && p->capacityAh > 0)
                    line += TextFormat("  %.0f%%", 100.0 * p->chargeAh / p->capacityAh);
                DrawText(line.c_str(), 16, GetScreenHeight() - 30, 18, YELLOW);
            }
        }
        EndDrawing();

        if (!shotPath.empty() && ++frame == 10) {
            TakeScreenshot(shotPath.c_str());
            break;
        }
    }

    renderer.Unload();
    CloseWindow();
    return 0;
}
