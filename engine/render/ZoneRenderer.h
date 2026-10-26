#pragma once

#include "raylib.h"

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
    void Draw(const RenderFrame& frame, const Camera3D& cam, int selectedEntity = -1);

    float ambient = 0.10f;  // light with all lamps off (0..1)

private:
    Model cube_{};
    Shader shader_{};
    int locLightPos_ = -1, locLightColor_ = -1, locLightCount_ = -1;
    int locAmbient_ = -1, locEmissive_ = -1, locViewPos_ = -1;
    bool ready_ = false;
};

} // namespace eon
