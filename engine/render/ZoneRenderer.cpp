#include "ZoneRenderer.h"

#include <algorithm>
#include <vector>

#include "raymath.h"
#include "rlgl.h"

namespace eon {

namespace {

const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform int lightCount;
uniform vec3 lightPos[8];
uniform vec3 lightColor[8];
uniform float ambient;
uniform float emissive;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    vec3 base = pow(colDiffuse.rgb, vec3(2.2));
    vec3 light = vec3(ambient) * (0.7 + 0.3 * n.y);
    for (int i = 0; i < lightCount; ++i) {
        vec3 L = lightPos[i] - fragPos;
        float d = length(L);
        L /= max(d, 0.001);
        float att = 1.0 / (1.0 + 0.35 * d * d);
        light += lightColor[i] * (max(dot(n, L), 0.0) + 0.12) * att;
    }
    vec3 c = base * light + base * emissive;
    c = c / (1.0 + c);
    c = pow(c, vec3(1.0 / 2.2));
    finalColor = vec4(c, colDiffuse.a);
}
)";

} // namespace

void FlyCamera::Init(Vector3 pos, Vector3 target) {
    cam.position = pos;
    cam.target = target;
    cam.up = {0, 1, 0};
    cam.fovy = 70.f;
    cam.projection = CAMERA_PERSPECTIVE;
}

void FlyCamera::Update(float dt, bool useWheel) {
    const float speed = (IsKeyDown(KEY_LEFT_SHIFT) ? 6.f : 2.f) * dt;
    Vector3 move{0, 0, 0};
    if (IsKeyDown(KEY_W)) move.x += speed;
    if (IsKeyDown(KEY_S)) move.x -= speed;
    if (IsKeyDown(KEY_D)) move.y += speed;
    if (IsKeyDown(KEY_A)) move.y -= speed;
    if (IsKeyDown(KEY_E)) move.z += speed;
    if (IsKeyDown(KEY_Q)) move.z -= speed;

    Vector3 rot{0, 0, 0};
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        const Vector2 d = GetMouseDelta();
        rot.x = d.x * 0.15f;
        rot.y = d.y * 0.15f;
    }
    UpdateCameraPro(&cam, move, rot, useWheel ? -GetMouseWheelMove() * 0.3f : 0.f);
}

bool ZoneRenderer::Init() {
    cube_ = LoadModelFromMesh(GenMeshCube(1.f, 1.f, 1.f));
    shader_ = LoadShaderFromMemory(kVS, kFS);
    shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
    shader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader_, "matNormal");
    locLightPos_ = GetShaderLocation(shader_, "lightPos");
    locLightColor_ = GetShaderLocation(shader_, "lightColor");
    locLightCount_ = GetShaderLocation(shader_, "lightCount");
    locAmbient_ = GetShaderLocation(shader_, "ambient");
    locEmissive_ = GetShaderLocation(shader_, "emissive");
    cube_.materials[0].shader = shader_;
    ready_ = IsShaderValid(shader_);
    return ready_;
}

void ZoneRenderer::Unload() {
    if (!ready_) return;
    cube_.materials[0].shader = Shader{};
    UnloadModel(cube_);
    UnloadShader(shader_);
    ready_ = false;
}

void ZoneRenderer::Draw(const RenderFrame& frame, const Camera3D& cam, int selectedEntity) {
    std::vector<Vector3> lightPos, lightCol;
    for (const RenderLight& l : frame.lights) {
        if (lightPos.size() >= 8) break;
        lightPos.push_back(ToRl(l.pos));
        lightCol.push_back(ToRl(l.color));
    }
    const int count = static_cast<int>(lightPos.size());
    SetShaderValue(shader_, locLightCount_, &count, SHADER_UNIFORM_INT);
    if (count > 0) {
        SetShaderValueV(shader_, locLightPos_, lightPos.data(), SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(shader_, locLightColor_, lightCol.data(), SHADER_UNIFORM_VEC3, count);
    }
    SetShaderValue(shader_, locAmbient_, &ambient, SHADER_UNIFORM_FLOAT);

    BeginMode3D(cam);
    for (const RenderPiece& p : frame.pieces) {
        SetShaderValue(shader_, locEmissive_, &p.emissive, SHADER_UNIFORM_FLOAT);
        Vec3 axis;
        float angle = 0.f;
        p.rot.ToAxisAngle(axis, angle);
        DrawModelEx(cube_, ToRl(p.pos), ToRl(axis), angle * RAD2DEG, ToRl(p.size), Color{p.color.r, p.color.g, p.color.b, 255});

        if (p.entity == selectedEntity && selectedEntity > 0) {
            rlPushMatrix();
            rlTranslatef(p.pos.x, p.pos.y, p.pos.z);
            rlRotatef(angle * RAD2DEG, axis.x, axis.y, axis.z);
            DrawCubeWires({0, 0, 0}, p.size.x + 0.004f, p.size.y + 0.004f, p.size.z + 0.004f, YELLOW);
            rlPopMatrix();
        }
    }
    EndMode3D();
}

} // namespace eon
