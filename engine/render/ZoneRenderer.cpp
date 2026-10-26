#include "ZoneRenderer.h"

#include <algorithm>
#include <cfloat>
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

Quaternion EntityRot(const Entity& e) {
    return QuaternionFromEuler(e.rotDeg.x * DEG2RAD, e.rotDeg.y * DEG2RAD, e.rotDeg.z * DEG2RAD);
}

Vector3 V(const Vec3& v) { return {v.x, v.y, v.z}; }

} // namespace

void FlyCamera::Init(Vector3 pos, Vector3 target) {
    cam.position = pos;
    cam.target = target;
    cam.up = {0, 1, 0};
    cam.fovy = 70.f;
    cam.projection = CAMERA_PERSPECTIVE;
}

void FlyCamera::Update(float dt) {
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
    UpdateCameraPro(&cam, move, rot, -GetMouseWheelMove() * 0.3f);
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

void ZoneRenderer::Draw(const World& world, const Camera3D& cam, int selectedId) {
    const Zone& zone = world.GetZone();

    // Lights come from lamps that are actually powered.
    std::vector<Vector3> lightPos, lightCol;
    for (const Entity& e : zone.entities) {
        if (e.type != "lamp" || e.part < 0) continue;
        const Part& p = world.GetCircuit().GetPart(e.part);
        if (p.brightness <= 0.001 || lightPos.size() >= 8) continue;
        const float k = static_cast<float>(p.brightness * p.ratedPower) * 0.4f;
        lightPos.push_back({e.pos.x, e.pos.y - e.size.y * 0.6f, e.pos.z});
        lightCol.push_back({k * 1.0f, k * 0.92f, k * 0.78f}); // warm incandescent
    }
    const int count = static_cast<int>(lightPos.size());
    SetShaderValue(shader_, locLightCount_, &count, SHADER_UNIFORM_INT);
    if (count > 0) {
        SetShaderValueV(shader_, locLightPos_, lightPos.data(), SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(shader_, locLightColor_, lightCol.data(), SHADER_UNIFORM_VEC3, count);
    }
    SetShaderValue(shader_, locAmbient_, &ambient, SHADER_UNIFORM_FLOAT);

    BeginMode3D(cam);
    for (const Entity& e : zone.entities) {
        float emissive = 0.f;
        if (e.type == "lamp" && e.part >= 0) {
            emissive = static_cast<float>(world.GetCircuit().GetPart(e.part).brightness) * 2.f;
        }
        SetShaderValue(shader_, locEmissive_, &emissive, SHADER_UNIFORM_FLOAT);

        Vector3 axis{0, 1, 0};
        float angle = 0.f;
        QuaternionToAxisAngle(EntityRot(e), &axis, &angle);
        DrawModelEx(cube_, V(e.pos), axis, angle * RAD2DEG, V(e.size), Color{e.color.r, e.color.g, e.color.b, 255});

        if (e.id == selectedId) {
            rlPushMatrix();
            rlTranslatef(e.pos.x, e.pos.y, e.pos.z);
            rlRotatef(angle * RAD2DEG, axis.x, axis.y, axis.z);
            DrawCubeWires({0, 0, 0}, e.size.x + 0.01f, e.size.y + 0.01f, e.size.z + 0.01f, YELLOW);
            rlPopMatrix();
        }
    }
    EndMode3D();
}

int ZoneRenderer::Pick(const World& world, Ray ray, float* outDist) {
    int best = -1;
    float bestDist = FLT_MAX;
    for (const Entity& e : world.GetZone().entities) {
        const Quaternion inv = QuaternionInvert(EntityRot(e));
        Ray local;
        local.position = Vector3RotateByQuaternion(Vector3Subtract(ray.position, V(e.pos)), inv);
        local.direction = Vector3RotateByQuaternion(ray.direction, inv);
        const Vector3 h{e.size.x * 0.5f, e.size.y * 0.5f, e.size.z * 0.5f};
        const RayCollision hit = GetRayCollisionBox(local, BoundingBox{Vector3Negate(h), h});
        if (hit.hit && hit.distance < bestDist) {
            bestDist = hit.distance;
            best = e.id;
        }
    }
    if (outDist) *outDist = bestDist;
    return best;
}

} // namespace eon
