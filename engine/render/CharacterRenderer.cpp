#include "CharacterRenderer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include "raymath.h"
#include "rlgl.h"

namespace eon {

namespace fs = std::filesystem;

namespace {

const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
out vec3 fragPos;
out vec3 fragNormal;
out vec2 fragUV;
void main() {
    fragPos = vertexPosition;          // vertices are already in world space
    fragNormal = vertexNormal;
    fragUV = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec2 fragUV;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int useTex;
uniform int lightCount;
uniform vec3 lightPos[8];
uniform vec3 lightColor[8];
uniform float ambient;
out vec4 finalColor;
void main() {
    vec4 tex = useTex == 1 ? texture(texture0, fragUV) : vec4(1.0);
    if (tex.a < 0.35) discard;                 // hair cards, lashes
    vec3 n = normalize(fragNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 base = pow(tex.rgb * colDiffuse.rgb, vec3(2.2));
    vec3 light = vec3(ambient) * (0.7 + 0.3 * n.y);
    for (int i = 0; i < lightCount; ++i) {
        vec3 L = lightPos[i] - fragPos;
        float d = length(L);
        L /= max(d, 0.001);
        float att = 1.0 / (1.0 + 0.35 * d * d);
        float ndl = dot(n, L);
        float wrap = max((ndl + 0.3) / 1.3, 0.0);   // cheap skin wrap lighting
        light += lightColor[i] * (wrap + 0.08) * att;
    }
    vec3 c = base * light;
    c = c / (1.0 + c);
    finalColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
)";

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

Texture2D CharacterRenderer::LoadTex(const std::string& path, const std::string& fbxDir, const std::vector<std::string>& dirs) {
    if (path.empty()) return Texture2D{};
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::vector<fs::path> tries = {fs::path(p), fs::path(fbxDir) / p};
    const std::string base = fs::path(p).filename().string();
    for (const std::string& d : dirs) tries.push_back(fs::path(d) / base);

    for (const fs::path& t : tries) {
        std::error_code ec;
        if (fs::exists(t, ec)) {
            Texture2D tex = LoadTexture(t.string().c_str());
            if (tex.id) {
                GenTextureMipmaps(&tex);
                SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
                ++loadedTextures_;
                return tex;
            }
        }
    }
    // case-insensitive file name search in the texture dirs (Windows-made assets on any OS)
    const std::string want = Lower(base);
    for (const std::string& d : dirs) {
        std::error_code ec;
        for (const auto& e : fs::recursive_directory_iterator(d, ec)) {
            if (e.is_regular_file() && Lower(e.path().filename().string()) == want) {
                Texture2D tex = LoadTexture(e.path().string().c_str());
                if (tex.id) {
                    GenTextureMipmaps(&tex);
                    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
                    ++loadedTextures_;
                    return tex;
                }
            }
        }
    }
    missing_.push_back(path);
    return Texture2D{};
}

bool CharacterRenderer::Init(const Character& ch, const std::vector<std::string>& textureDirs) {
    const CharacterAsset& a = *ch.Asset();
    shader_ = LoadShaderFromMemory(kVS, kFS);
    locLightPos_ = GetShaderLocation(shader_, "lightPos");
    locLightColor_ = GetShaderLocation(shader_, "lightColor");
    locLightCount_ = GetShaderLocation(shader_, "lightCount");
    locAmbient_ = GetShaderLocation(shader_, "ambient");
    locUseTex_ = GetShaderLocation(shader_, "useTex");
    material_ = LoadMaterialDefault();
    material_.shader = shader_;

    const std::string fbxDir = fs::path(a.sourcePath).parent_path().string();
    mats_.resize(a.materials.size());
    for (size_t i = 0; i < a.materials.size(); ++i) {
        const MaterialInfo& mi = a.materials[i];
        mats_[i].albedo = LoadTex(mi.baseColorTex, fbxDir, textureDirs);
        mats_[i].hasAlbedo = mats_[i].albedo.id != 0;
        if (!mats_[i].hasAlbedo) {
            auto c = [](float v) { return static_cast<unsigned char>(std::clamp(v, 0.f, 1.f) * 255.f); };
            mats_[i].tint = {c(mi.baseColor.x), c(mi.baseColor.y), c(mi.baseColor.z), 255};
        } else {
            mats_[i].tint = WHITE;
        }
    }

    visible_.assign(a.meshes.size(), true);
    constexpr size_t kMaxVerts = 65000;
    for (size_t mi = 0; mi < a.meshes.size(); ++mi) {
        const SkinMesh& m = a.meshes[mi];
        size_t t = 0;
        while (t < m.indices.size()) {
            Chunk c;
            c.assetMesh = mi;
            c.material = m.material;
            std::unordered_map<uint32_t, uint16_t> remap;
            std::vector<uint16_t> idx;
            while (t < m.indices.size()) {
                size_t needed = 0;
                for (int k = 0; k < 3; ++k) if (!remap.count(m.indices[t + k])) ++needed;
                if (remap.size() + needed > kMaxVerts) break;
                for (int k = 0; k < 3; ++k) {
                    const uint32_t v = m.indices[t + k];
                    auto it = remap.find(v);
                    if (it == remap.end()) {
                        it = remap.emplace(v, static_cast<uint16_t>(c.srcVertex.size())).first;
                        c.srcVertex.push_back(v);
                    }
                    idx.push_back(it->second);
                }
                t += 3;
            }
            const int nv = static_cast<int>(c.srcVertex.size());
            Mesh& mesh = c.mesh;
            mesh.vertexCount = nv;
            mesh.triangleCount = static_cast<int>(idx.size() / 3);
            mesh.vertices = static_cast<float*>(MemAlloc(nv * 3 * sizeof(float)));
            mesh.normals = static_cast<float*>(MemAlloc(nv * 3 * sizeof(float)));
            mesh.texcoords = static_cast<float*>(MemAlloc(nv * 2 * sizeof(float)));
            mesh.indices = static_cast<unsigned short*>(MemAlloc(idx.size() * sizeof(unsigned short)));
            for (int v = 0; v < nv; ++v) {
                const uint32_t s = c.srcVertex[v];
                std::memcpy(&mesh.vertices[v * 3], &m.pos[s], 3 * sizeof(float));
                std::memcpy(&mesh.normals[v * 3], &m.nrm[s], 3 * sizeof(float));
                mesh.texcoords[v * 2] = m.uv[s * 2];
                mesh.texcoords[v * 2 + 1] = m.uv[s * 2 + 1];
            }
            std::memcpy(mesh.indices, idx.data(), idx.size() * sizeof(unsigned short));
            UploadMesh(&mesh, true);
            chunks_.push_back(std::move(c));
        }
    }
    ready_ = IsShaderValid(shader_);
    Upload(ch);
    return ready_;
}

void CharacterRenderer::Unload() {
    for (Chunk& c : chunks_) UnloadMesh(c.mesh);
    chunks_.clear();
    for (Mat& m : mats_) if (m.hasAlbedo) UnloadTexture(m.albedo);
    mats_.clear();
    if (ready_) UnloadShader(shader_);
    ready_ = false;
}

void CharacterRenderer::SetMeshVisible(size_t assetMesh, bool v) {
    if (assetMesh < visible_.size()) visible_[assetMesh] = v;
}

void CharacterRenderer::Upload(const Character& ch) {
    for (Chunk& c : chunks_) {
        if (!visible_[c.assetMesh]) continue;
        const auto& P = ch.SkinnedPos(c.assetMesh);
        const auto& N = ch.SkinnedNrm(c.assetMesh);
        const size_t nv = c.srcVertex.size();
        scratch_.resize(nv * 3);
        for (size_t v = 0; v < nv; ++v) std::memcpy(&scratch_[v * 3], &P[c.srcVertex[v]], 3 * sizeof(float));
        UpdateMeshBuffer(c.mesh, 0, scratch_.data(), static_cast<int>(nv * 3 * sizeof(float)), 0);
        for (size_t v = 0; v < nv; ++v) std::memcpy(&scratch_[v * 3], &N[c.srcVertex[v]], 3 * sizeof(float));
        UpdateMeshBuffer(c.mesh, 2, scratch_.data(), static_cast<int>(nv * 3 * sizeof(float)), 0);
    }
}

void CharacterRenderer::Draw(const RenderFrame& lighting, const Camera3D& cam, float ambient) {
    std::vector<Vector3> lp, lc;
    for (const RenderLight& l : lighting.lights) {
        if (lp.size() >= 8) break;
        lp.push_back({l.pos.x, l.pos.y, l.pos.z});
        lc.push_back({l.color.x, l.color.y, l.color.z});
    }
    const int count = static_cast<int>(lp.size());
    SetShaderValue(shader_, locLightCount_, &count, SHADER_UNIFORM_INT);
    if (count) {
        SetShaderValueV(shader_, locLightPos_, lp.data(), SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(shader_, locLightColor_, lc.data(), SHADER_UNIFORM_VEC3, count);
    }
    SetShaderValue(shader_, locAmbient_, &ambient, SHADER_UNIFORM_FLOAT);

    BeginMode3D(cam);
    rlDisableBackfaceCulling();   // thin clothing and hair are often single-sided
    for (const Chunk& c : chunks_) {
        if (!visible_[c.assetMesh]) continue;
        int useTex = 0;
        if (c.material >= 0 && c.material < (int)mats_.size()) {
            const Mat& m = mats_[c.material];
            material_.maps[MATERIAL_MAP_DIFFUSE].color = m.tint;
            if (m.hasAlbedo) { material_.maps[MATERIAL_MAP_DIFFUSE].texture = m.albedo; useTex = 1; }
        } else {
            material_.maps[MATERIAL_MAP_DIFFUSE].color = {230, 190, 170, 255};
        }
        if (!useTex) material_.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        SetShaderValue(shader_, locUseTex_, &useTex, SHADER_UNIFORM_INT);
        DrawMesh(c.mesh, material_, MatrixIdentity());
    }
    rlEnableBackfaceCulling();
    EndMode3D();
}

} // namespace eon
