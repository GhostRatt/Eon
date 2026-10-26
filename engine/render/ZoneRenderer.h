#pragma once

#include "raylib.h"

#include "core/World.h"

namespace eon {

// Free-fly camera for the operator (hold right mouse to look, WASD move, Q/E down/up, Shift fast).
struct FlyCamera {
    Camera3D cam{};
    void Init(Vector3 pos, Vector3 target);
    void Update(float dt);
};

class ZoneRenderer {
public:
    bool Init();
    void Unload();

    // Draws the zone lit by its lamps. selectedId highlights an entity (operator/editor only).
    void Draw(const World& world, const Camera3D& cam, int selectedId = -1);

    // Ray pick against oriented entity boxes. Returns entity id or -1.
    static int Pick(const World& world, Ray ray, float* outDist = nullptr);

    float ambient = 0.10f;  // light with all lamps off (0..1)

private:
    Model cube_{};
    Shader shader_{};
    int locLightPos_ = -1, locLightColor_ = -1, locLightCount_ = -1;
    int locAmbient_ = -1, locEmissive_ = -1, locViewPos_ = -1;
    bool ready_ = false;
};

} // namespace eon
