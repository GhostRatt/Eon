#pragma once

#include "raylib.h"
#include <unordered_map>
#include <vector>

#include "core/World.h"

namespace eon {

// Free-fly camera for the operator (hold right mouse to look, WASD move, Q/E down/up, Shift fast).
struct FlyCamera {
    Camera3D cam{};
    void Init(Vector3 pos, Vector3 target);
    void Update(float dt, bool useWheel = true);
};

inline Vec3 ToEon(Vector3 v) { return {v.x, v.y, v.z}; }
inline Vector3 ToRl(const Vec3& v) { return {v.x, v.y, v.z}; }

class ZoneRenderer {
public:
    bool Init();
    void Unload();

    // Draws a render frame lit by its lights. selectedEntity outlines that entity (operator/editor only).
    // withTranslucent=false: call DrawTranslucent yourself after the character so water is drawn over her legs.
    void Draw(const RenderFrame& frame, const Camera3D& cam, int selectedEntity = -1, bool withTranslucent = true);
    void DrawTranslucent(const Camera3D& cam);   // the translucent pieces of the last Draw

    float ambient = 0.10f;  // light with all lamps off (0..1)

private:
    struct Batch { Mesh mesh{}; };
    static constexpr size_t kBoxesPerBatch = 2700;   // 24 verts each: under the 16-bit index limit
    Batch MakeBatch();
    std::vector<Batch> batches_;
    Batch glass_;
    std::vector<const RenderPiece*> translucent_;
    struct Cached { Mesh mesh{}; uint32_t version = 0; };
    std::unordered_map<uint64_t, Cached> meshCache_;
    void FillBoxes(Batch& b, const RenderPiece* const* pieces, size_t count, bool alphaIsOpacity);
    uint64_t MeshKey(const RenderMesh& m) const;
    void CachedMesh(const RenderMesh& m);
    Material material_{};
    Shader shader_{};
    int locLightPos_ = -1, locLightColor_ = -1, locLightCount_ = -1;
    int locAmbient_ = -1, locTranslucent_ = -1, locViewPos_ = -1, locSunDir_ = -1, locSunColor_ = -1;
    bool ready_ = false;
};

} // namespace eon
