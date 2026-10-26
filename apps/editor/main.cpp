// eon_editor — build and edit zones. Step 1: select, move, rotate, duplicate, add, delete, save.
// Full tool UI (part library, wire tool, play-in-editor) arrives in step 3.

#include <algorithm>
#include <cmath>
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

// Rebuild runtime after an edit but keep switch states and battery charge.
void Rebuild(World& w) {
    Zone& z = w.GetZone();
    for (Entity& e : z.entities) {
        const Part* p = e.part >= 0 ? &w.GetCircuit().GetPart(e.part) : nullptr;
        if (!p) continue;
        if (e.type == "switch") e.params["closed"] = p->closed;
        if (e.type == "battery" && p->capacityAh > 0) e.params["charge"] = p->chargeAh / p->capacityAh;
    }
    w.Reset();
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : ContentDir() + "/zones/test_zone_01.json";

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Eon Editor");
    SetTargetFPS(120);

    ZoneRenderer renderer;
    renderer.Init();
    renderer.ambient = 0.6f; // editor: always readable

    World world;
    std::string status;
    {
        std::string err;
        status = world.Load(path, &err) ? "Opened " + path : "Open failed: " + err;
    }

    FlyCamera fly;
    fly.Init({0, 1.8f, 2.8f}, {0, 0.8f, 0});

    int selected = -1;
    bool dirty = false;
    bool editorLight = true;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        fly.Update(dt);
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            selected = ZoneRenderer::Pick(world, GetScreenToWorldRay(GetMousePosition(), fly.cam));
        }

        Entity* sel = selected > 0 ? world.GetZone().Find(selected) : nullptr;
        bool changed = false;

        if (sel && !ctrl) {
            const float step = shift ? 0.01f : 0.05f;
            if (IsKeyPressed(KEY_LEFT))      { sel->pos.x -= step; changed = true; }
            if (IsKeyPressed(KEY_RIGHT))     { sel->pos.x += step; changed = true; }
            if (IsKeyPressed(KEY_UP))        { sel->pos.z -= step; changed = true; }
            if (IsKeyPressed(KEY_DOWN))      { sel->pos.z += step; changed = true; }
            if (IsKeyPressed(KEY_PAGE_UP))   { sel->pos.y += step; changed = true; }
            if (IsKeyPressed(KEY_PAGE_DOWN)) { sel->pos.y -= step; changed = true; }
            if (IsKeyPressed(KEY_R))         { sel->rotDeg.y = fmodf(sel->rotDeg.y + 15.f, 360.f); changed = true; }
            if (IsKeyPressed(KEY_F))         { sel->rotDeg.y = fmodf(sel->rotDeg.y + 345.f, 360.f); changed = true; }
            if (IsKeyPressed(KEY_SPACE)) {
                if (sel->type == "switch") world.Toggle(selected);
            }
            if (IsKeyPressed(KEY_DELETE)) {
                Zone& z = world.GetZone();
                const int id = selected;
                z.links.erase(std::remove_if(z.links.begin(), z.links.end(), [id](const TerminalLink& l) {
                    return l.entityA == id || l.entityB == id; }), z.links.end());
                z.entities.erase(std::remove_if(z.entities.begin(), z.entities.end(), [id](const Entity& e) {
                    return e.id == id; }), z.entities.end());
                selected = -1;
                sel = nullptr;
                changed = true;
            }
        }
        // Momentary button preview while Space is held
        if (sel && sel->type == "button") world.SetPressed(selected, IsKeyDown(KEY_SPACE));

        if (ctrl && IsKeyPressed(KEY_D) && sel) {
            Entity copy = *sel;
            copy.id = world.GetZone().NextId();
            copy.name += "_copy";
            copy.pos.x += 0.1f;
            world.GetZone().entities.push_back(copy);
            selected = copy.id;
            changed = true;
        }
        if (IsKeyPressed(KEY_N)) {
            Entity box;
            box.id = world.GetZone().NextId();
            box.name = TextFormat("Box_%d", box.id);
            const Vector3 fwd = Vector3Normalize(Vector3Subtract(fly.cam.target, fly.cam.position));
            const Vector3 p = Vector3Add(fly.cam.position, Vector3Scale(fwd, 1.5f));
            box.pos = {p.x, std::max(p.y, 0.15f), p.z};
            box.size = {0.3f, 0.3f, 0.3f};
            box.massKg = 2.f;
            world.GetZone().entities.push_back(box);
            selected = box.id;
            changed = true;
        }
        if (ctrl && IsKeyPressed(KEY_S)) {
            Rebuild(world);
            std::string err;
            status = SaveZone(path, world.GetZone(), &err) ? "Saved " + path : "Save failed: " + err;
            dirty = false;
        }
        if (ctrl && IsKeyPressed(KEY_O)) {
            std::string err;
            status = world.Load(path, &err) ? "Reloaded" : "Reload failed: " + err;
            selected = -1;
            dirty = false;
        }
        if (IsKeyPressed(KEY_L)) {
            editorLight = !editorLight;
            renderer.ambient = editorLight ? 0.6f : 0.1f;
        }

        if (changed) { Rebuild(world); dirty = true; }
        world.Tick(dt);

        BeginDrawing();
        ClearBackground({30, 32, 36, 255});
        renderer.Draw(world, fly.cam, selected);

        BeginMode3D(fly.cam);
        DrawGrid(20, 0.5f);
        EndMode3D();

        DrawRectangle(8, 8, 560, 150, Fade(BLACK, 0.6f));
        DrawText(TextFormat("%s%s", world.GetZone().name.c_str(), dirty ? "  *unsaved*" : ""), 16, 14, 20, RAYWHITE);
        DrawText("LMB select  Arrows/PgUp/PgDn move (Shift fine)  R/F rotate", 16, 42, 16, LIGHTGRAY);
        DrawText("N new box  Ctrl+D duplicate  Del delete  Space press/flip", 16, 62, 16, LIGHTGRAY);
        DrawText("Ctrl+S save  Ctrl+O reload  L editor light  RMB look  WASD/QE fly", 16, 82, 16, LIGHTGRAY);
        DrawText(status.c_str(), 16, 110, 16, SKYBLUE);

        if (sel) {
            const Part* p = world.PartOf(selected);
            const int x = GetScreenWidth() - 420;
            DrawRectangle(x - 8, 8, 412, 200, Fade(BLACK, 0.6f));
            DrawText(TextFormat("#%d %s", sel->id, sel->name.c_str()), x, 14, 20, YELLOW);
            DrawText(TextFormat("type %s   mass %.3f kg", sel->type.c_str(), sel->massKg), x, 40, 16, RAYWHITE);
            DrawText(TextFormat("pos %.3f %.3f %.3f", sel->pos.x, sel->pos.y, sel->pos.z), x, 60, 16, RAYWHITE);
            DrawText(TextFormat("rot %.0f %.0f %.0f", sel->rotDeg.x, sel->rotDeg.y, sel->rotDeg.z), x, 80, 16, RAYWHITE);
            DrawText(TextFormat("size %.3f %.3f %.3f", sel->size.x, sel->size.y, sel->size.z), x, 100, 16, RAYWHITE);
            if (p) {
                DrawText(TextFormat("%.3f V   %.4f A   %.3f W", p->v, p->i, p->power), x, 124, 16, GREEN);
                if (sel->type == "battery" && p->capacityAh > 0)
                    DrawText(TextFormat("charge %.1f%%", 100.0 * p->chargeAh / p->capacityAh), x, 144, 16, GREEN);
                if (sel->type == "lamp") DrawText(TextFormat("brightness %.0f%%", 100.0 * p->brightness), x, 144, 16, GREEN);
            }
            const std::string params = sel->params.dump();
            DrawText(params.size() > 48 ? (params.substr(0, 45) + "...").c_str() : params.c_str(), x, 172, 14, GRAY);
        }
        EndDrawing();
    }

    renderer.Unload();
    CloseWindow();
    return 0;
}
