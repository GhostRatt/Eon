#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.h"

namespace eon {

struct Bone {
    std::string name;
    int parent = -1;
    Transform bindLocal;   // rest pose relative to parent
    Mat4 bindWorld;        // rest pose in model space
    Mat4 invBind;          // model space -> bone space at rest (skinning)
};

struct Morph {
    std::string name;
    std::vector<uint32_t> index;   // affected vertices
    std::vector<Vec3> dpos;        // position delta per affected vertex
};

struct SkinMesh {
    std::string name;
    int material = -1;
    std::vector<Vec3> pos, nrm;
    std::vector<float> uv;                         // 2 per vertex
    std::vector<std::array<uint16_t, 4>> joints;   // bone indices
    std::vector<std::array<float, 4>> weights;     // sum to 1
    std::vector<uint32_t> indices;                 // triangles
    std::vector<Morph> morphs;
};

struct MaterialInfo {
    std::string name;
    Vec3 baseColor{0.8f, 0.8f, 0.8f};
    std::string baseColorTex, normalTex, roughnessTex;
};

// Everything needed to draw and animate one character. Model space: meters, Y up, character faces +Z.
struct CharacterAsset {
    std::vector<Bone> bones;
    std::vector<SkinMesh> meshes;
    std::vector<MaterialInfo> materials;
    std::string sourcePath;

    int FindBone(const std::string& name) const;   // exact
    // Case-insensitive match against several candidate names (handles Mannequin, ARP, Mixamo, Blender .L/.R)
    int FindBoneAny(std::initializer_list<const char*> candidates) const;
    size_t VertexCount() const;
    size_t TriangleCount() const;
};

struct ImportOptions {
    float targetHeight = 0.f;      // >0: rescale so the rest pose is this tall (m)
    bool loadMorphs = true;
};

bool ImportFbx(const std::string& path, CharacterAsset& out, const ImportOptions& opt = {}, std::string* error = nullptr);

} // namespace eon
