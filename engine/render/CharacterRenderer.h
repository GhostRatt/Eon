#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"

#include "character/Character.h"
#include "core/World.h"

namespace eon {

// Uploads a skinned character and redraws it every frame from Character::Skin() output.
// Large meshes are split into <=65k-vertex chunks (raylib uses 16-bit indices).
// Per-material fixes from character.json (FBX files often lose texture links or carry helper meshes).
struct MaterialOverride {
    std::string baseColor, opacity;   // file names (searched in texture dirs)
    bool hidden = false;
    bool hasColor = false;
    Vec3 color{1, 1, 1};
};

class CharacterRenderer {
public:
    // Keys match material names with Blender's ".001"-style suffixes ignored.
    std::unordered_map<std::string, MaterialOverride> overrides;

    // textureDirs: extra folders searched by file name when an FBX texture path doesn't resolve
    bool Init(const Character& ch, const std::vector<std::string>& textureDirs);
    void Unload();

    void Upload(const Character& ch);   // push skinned vertices to the GPU
    void Draw(const RenderFrame& lighting, const Camera3D& cam, float ambient);

    // Optional: per-mesh visibility (clothing layers, censoring)
    void SetMeshVisible(size_t assetMesh, bool visible);
    size_t LoadedTextures() const { return loadedTextures_; }
    std::vector<std::string> MissingTextures() const { return missing_; }

private:
    struct Chunk {
        Mesh mesh{};
        size_t assetMesh = 0;
        std::vector<uint32_t> srcVertex;   // chunk vertex -> asset mesh vertex
        int material = -1;
    };
    struct Mat {
        Texture2D albedo{};
        bool hasAlbedo = false;
        Color tint{230, 190, 170, 255};
        bool hidden = false;
    };

    Texture2D LoadTex(const std::string& path, const std::string& fbxDir, const std::vector<std::string>& dirs,
                      const std::string& opacityPath = {});
    std::string FindFile(const std::string& path, const std::string& fbxDir, const std::vector<std::string>& dirs);

    std::vector<Chunk> chunks_;
    std::vector<Mat> mats_;
    std::vector<bool> visible_;
    std::vector<float> scratch_;
    Shader shader_{};
    Material material_{};
    int locLightPos_ = -1, locLightColor_ = -1, locLightCount_ = -1, locAmbient_ = -1, locUseTex_ = -1;
    size_t loadedTextures_ = 0;
    std::vector<std::string> missing_;
    bool ready_ = false;
};

} // namespace eon
