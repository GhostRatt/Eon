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
in vec4 vertexColor;          // rgb = albedo, a = emissive / 4
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform int lightCount;
uniform vec3 lightPos[8];
uniform vec3 lightColor[8];
uniform float ambient;
uniform int translucent;      // 1: vertex alpha is opacity (water), not emission
uniform vec3 sunDir;
uniform vec3 sunColor;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    vec3 base = pow(fragColor.rgb, vec3(2.2));
    float emissive = translucent == 1 ? 0.0 : fragColor.a * 4.0;
    vec3 light = vec3(ambient) * (0.7 + 0.3 * n.y);
    light += sunColor * max(dot(n, -sunDir), 0.0) + sunColor * 0.28 * (0.6 + 0.4 * n.y);   // sun + sky fill
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
    finalColor = vec4(c, translucent == 1 ? fragColor.a : 1.0);
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
    shader_ = LoadShaderFromMemory(kVS, kFS);
    shader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader_, "matModel");
    shader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(shader_, "matNormal");
    locLightPos_ = GetShaderLocation(shader_, "lightPos");
    locLightColor_ = GetShaderLocation(shader_, "lightColor");
    locLightCount_ = GetShaderLocation(shader_, "lightCount");
    locAmbient_ = GetShaderLocation(shader_, "ambient");
    locTranslucent_ = GetShaderLocation(shader_, "translucent");
    locSunDir_ = GetShaderLocation(shader_, "sunDir");
    locSunColor_ = GetShaderLocation(shader_, "sunColor");
    material_ = LoadMaterialDefault();
    material_.shader = shader_;
    ready_ = IsShaderValid(shader_);
    return ready_;
}

ZoneRenderer::Batch ZoneRenderer::MakeBatch() {
    Batch b;
    Mesh& m = b.mesh;
    const int verts = static_cast<int>(kBoxesPerBatch * 24);
    m.vertexCount = verts;
    m.triangleCount = static_cast<int>(kBoxesPerBatch * 12);
    m.vertices = static_cast<float*>(MemAlloc(verts * 3 * sizeof(float)));
    m.normals = static_cast<float*>(MemAlloc(verts * 3 * sizeof(float)));
    m.colors = static_cast<unsigned char*>(MemAlloc(verts * 4));
    m.indices = static_cast<unsigned short*>(MemAlloc(kBoxesPerBatch * 36 * sizeof(unsigned short)));
    for (size_t k = 0; k < kBoxesPerBatch; ++k)
        for (int f = 0; f < 6; ++f) {
            const unsigned short o = static_cast<unsigned short>(k * 24 + f * 4);
            const unsigned short q[6] = {o, static_cast<unsigned short>(o + 1), static_cast<unsigned short>(o + 2), o, static_cast<unsigned short>(o + 2), static_cast<unsigned short>(o + 3)};
            std::copy(q, q + 6, m.indices + k * 36 + f * 6);
        }
    UploadMesh(&m, true);   // dynamic: positions/normals/colors rewritten every frame
    return b;
}

void ZoneRenderer::Unload() {
    if (!ready_) return;
    for (Batch& b : batches_) UnloadMesh(b.mesh);
    batches_.clear();
    for (auto& [k, m] : meshCache_) UnloadMesh(m.mesh);
    meshCache_.clear();
    if (glass_.mesh.vertexCount) UnloadMesh(glass_.mesh);
    material_.shader = Shader{};
    UnloadMaterial(material_);
    UnloadShader(shader_);
    ready_ = false;
}

void ZoneRenderer::Draw(const RenderFrame& frame, const Camera3D& cam, int selectedEntity, bool withTranslucent) {
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
    SetShaderValue(shader_, locSunDir_, &frame.sunDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, locSunColor_, &frame.sunColor, SHADER_UNIFORM_VEC3);

    // every opaque box goes into a few big dynamic meshes: one draw call per ~2700 boxes instead of one per box
    std::vector<const RenderPiece*> opaque, clear;
    opaque.reserve(frame.pieces.size());
    for (const RenderPiece& p : frame.pieces) (p.alpha < 0.999f ? clear : opaque).push_back(&p);
    const size_t n = opaque.size();
    const size_t nb = (n + kBoxesPerBatch - 1) / kBoxesPerBatch;
    while (batches_.size() < nb) batches_.push_back(MakeBatch());
    for (size_t bi = 0; bi < nb; ++bi) {
        const size_t first = bi * kBoxesPerBatch, count = std::min(kBoxesPerBatch, n - first);
        FillBoxes(batches_[bi], opaque.data() + first, count, false);
    }
    // terrain and other meshes: uploaded once per version
    for (const RenderMesh* m : frame.meshes) CachedMesh(*m);
    translucent_ = clear;
    const int zero = 0;
    SetShaderValue(shader_, locTranslucent_, &zero, SHADER_UNIFORM_INT);
    BeginMode3D(cam);
    rlDisableBackfaceCulling();   // winding of the generated faces isn't guaranteed; boxes are cheap to draw both sides
    for (size_t bi = 0; bi < nb; ++bi) DrawMesh(batches_[bi].mesh, material_, MatrixIdentity());
    for (const RenderMesh* m : frame.meshes) {
        auto it = meshCache_.find(MeshKey(*m));
        if (it != meshCache_.end()) DrawMesh(it->second.mesh, material_, MatrixIdentity());
    }
    rlEnableBackfaceCulling();
    if (selectedEntity > 0)
        for (const RenderPiece& p : frame.pieces) {
            if (p.entity != selectedEntity) continue;
            Vec3 axis;
            float angle = 0.f;
            p.rot.ToAxisAngle(axis, angle);
            rlPushMatrix();
            rlTranslatef(p.pos.x, p.pos.y, p.pos.z);
            rlRotatef(angle * RAD2DEG, axis.x, axis.y, axis.z);
            DrawCubeWires({0, 0, 0}, p.size.x + 0.004f, p.size.y + 0.004f, p.size.z + 0.004f, YELLOW);
            rlPopMatrix();
        }
    EndMode3D();
    if (withTranslucent) DrawTranslucent(cam);
}

void ZoneRenderer::DrawTranslucent(const Camera3D& cam) {
    if (translucent_.empty()) return;
    if (!glass_.mesh.vertexCount) glass_ = MakeBatch();
    const size_t count = std::min(kBoxesPerBatch, translucent_.size());
    FillBoxes(glass_, translucent_.data(), count, true);
    const int one = 1, zero = 0;
    SetShaderValue(shader_, locTranslucent_, &one, SHADER_UNIFORM_INT);
    BeginMode3D(cam);
    BeginBlendMode(BLEND_ALPHA);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawMesh(glass_.mesh, material_, MatrixIdentity());
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
    EndMode3D();
    SetShaderValue(shader_, locTranslucent_, &zero, SHADER_UNIFORM_INT);
}

void ZoneRenderer::FillBoxes(Batch& b, const RenderPiece* const* pieces, size_t count, bool alphaIsOpacity) {
    static const float F[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    float* P = b.mesh.vertices;
    float* N = b.mesh.normals;
    unsigned char* C = b.mesh.colors;
    for (size_t k = 0; k < count; ++k) {
        const RenderPiece& p = *pieces[k];
        const Vec3 ax = p.rot.Rotate({1, 0, 0}), ay = p.rot.Rotate({0, 1, 0}), az = p.rot.Rotate({0, 0, 1});
        const Vec3 hx = ax * (0.5f * p.size.x), hy = ay * (0.5f * p.size.y), hz = az * (0.5f * p.size.z);
        const unsigned char a = static_cast<unsigned char>(std::clamp(alphaIsOpacity ? p.alpha : p.emissive * 0.25f, 0.f, 1.f) * 255.f);
        for (int f = 0; f < 6; ++f) {
            const Vec3 nrm = ax * F[f][0] + ay * F[f][1] + az * F[f][2];
            const Vec3 c = p.pos + hx * F[f][0] + hy * F[f][1] + hz * F[f][2];
            const Vec3 u = f < 2 ? hy : hx, v = f < 2 ? hz : (f < 4 ? hz : hy);
            const Vec3 corner[4] = {c - u - v, c + u - v, c + u + v, c - u + v};
            for (int q = 0; q < 4; ++q) {
                *P++ = corner[q].x; *P++ = corner[q].y; *P++ = corner[q].z;
                *N++ = nrm.x; *N++ = nrm.y; *N++ = nrm.z;
                *C++ = p.color.r; *C++ = p.color.g; *C++ = p.color.b; *C++ = a;
            }
        }
    }
    const int verts = static_cast<int>(count * 24);
    UpdateMeshBuffer(b.mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, b.mesh.vertices, verts * 3 * sizeof(float), 0);
    UpdateMeshBuffer(b.mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, b.mesh.normals, verts * 3 * sizeof(float), 0);
    UpdateMeshBuffer(b.mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, b.mesh.colors, verts * 4, 0);
    b.mesh.triangleCount = static_cast<int>(count * 12);
}

uint64_t ZoneRenderer::MeshKey(const RenderMesh& m) const {
    return (static_cast<uint64_t>(static_cast<uint32_t>(m.entity)) << 32) ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&m) & 0xffffffffu);
}

void ZoneRenderer::CachedMesh(const RenderMesh& rm) {
    const uint64_t key = MeshKey(rm);
    auto it = meshCache_.find(key);
    if (it != meshCache_.end() && it->second.version == rm.version) return;
    if (it != meshCache_.end()) { UnloadMesh(it->second.mesh); meshCache_.erase(it); }
    if (rm.pos.empty() || rm.pos.size() > 65535) return;
    Mesh m{};
    m.vertexCount = static_cast<int>(rm.pos.size());
    m.triangleCount = static_cast<int>(rm.idx.size() / 3);
    m.vertices = static_cast<float*>(MemAlloc(m.vertexCount * 3 * sizeof(float)));
    m.normals = static_cast<float*>(MemAlloc(m.vertexCount * 3 * sizeof(float)));
    m.colors = static_cast<unsigned char*>(MemAlloc(m.vertexCount * 4));
    m.indices = static_cast<unsigned short*>(MemAlloc(rm.idx.size() * sizeof(unsigned short)));
    for (size_t v = 0; v < rm.pos.size(); ++v) {
        m.vertices[v * 3] = rm.pos[v].x; m.vertices[v * 3 + 1] = rm.pos[v].y; m.vertices[v * 3 + 2] = rm.pos[v].z;
        m.normals[v * 3] = rm.nrm[v].x; m.normals[v * 3 + 1] = rm.nrm[v].y; m.normals[v * 3 + 2] = rm.nrm[v].z;
        m.colors[v * 4] = rm.col[v].r; m.colors[v * 4 + 1] = rm.col[v].g; m.colors[v * 4 + 2] = rm.col[v].b; m.colors[v * 4 + 3] = 0;
    }
    for (size_t i = 0; i < rm.idx.size(); ++i) m.indices[i] = static_cast<unsigned short>(rm.idx[i]);
    UploadMesh(&m, false);
    if (meshCache_.size() > 256)   // tiles of meshes that no longer exist
        for (auto e = meshCache_.begin(); e != meshCache_.end();) { UnloadMesh(e->second.mesh); e = meshCache_.erase(e); }
    meshCache_[key] = {m, rm.version};
}

} // namespace eon
