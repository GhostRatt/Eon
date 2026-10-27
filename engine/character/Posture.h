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
};

// Builds the full pose for this frame (resets to bind first). Call before soft tissue/limb collision.
void ApplyPosture(Character& ch, const PostureParams& p);

const char* MotionName(TestMotion m);

} // namespace eon
