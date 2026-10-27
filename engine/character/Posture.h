#pragma once

#include "Character.h"

namespace eon {

// Stand-in body driver until Jenny's motor system exists: procedural standing pose built with IK,
// plus test motions for checking soft tissue and limb collision.
enum class TestMotion { Idle, Bounce, WalkInPlace, HandsOnChest, HandsOnHips, ArmsCrossed, Turn };

struct PostureParams {
    TestMotion motion = TestMotion::Idle;
    float time = 0.f;
    float breathRate = 0.25f;   // Hz (~15 breaths/min), used when breathPhase < 0
    float breathPhase = -1.f;   // 0..1 cycle position from the body sim (>= 0 overrides breathRate)
    float breathDepth = 0.5f;   // 0..1
    float shiver = 0.f;         // 0..1 fine tremor when cold

    // Motor overrides (set by Motor while she walks/reaches)
    Vec3 pelvisOffset;                  // world-space shift of the hips (gait bob)
    bool footOverride = false;
    Vec3 footTarget[2];                 // world positions of the feet
    bool handOverride[2] = {false, false};
    Vec3 handTarget[2];                 // world positions of the hands (her physical hands)
    float armSwing[2] = {0.f, 0.f};     // forward/back swing of relaxed arms (m)
    float lean = 0.f;                   // bend forward at the waist (radians), e.g. reaching across a table
    float leanTwist = 0.f;              // turn the shoulders toward the reaching side (radians)
    bool hasLook = false;
    Vec3 lookAt;
};

// Builds the full pose for this frame (resets to bind first). Call before soft tissue/limb collision.
void ApplyPosture(Character& ch, const PostureParams& p);

const char* MotionName(TestMotion m);

} // namespace eon
