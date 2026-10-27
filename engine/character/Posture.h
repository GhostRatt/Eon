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

    // Stance controls (crouch / kneel / crawl / lie...)
    Quat bodyTilt;                      // world rotation of the whole body about the hips (pitch to lie down)
    bool kneePoleOverride = false;
    Vec3 kneePole[2];                   // world points the knees bend toward
    bool footFollow = false;            // feet follow the shins (pointed toes: kneeling, lying) instead of staying flat
    Vec3 footNormal[2] = {{0, 1, 0}, {0, 1, 0}};   // ground normal under each foot (flat feet on slopes)
    bool elbowPoleOverride[2] = {false, false};
    Vec3 elbowPole[2];
    bool palmDown[2] = {false, false};  // planted hand: palm flat on the surface below
    float headPitch = 0.f;              // extra head nod (radians, + = chin up) e.g. lifting the head when prone
    float floorY = -1e9f;               // surface she is on: toes/thumbs are kept above it
    float footPitch[2] = {0.f, 0.f};    // extra point (+) of the feet when they follow the shins (lying, kneeling)
    float headRoll = 0.f;               // tilt the head sideways about headRollAxis (lying on a side)
    Vec3 headRollAxis{0, 0, 1};

    // Hands: palm orientation and fingers
    bool orientHand[2] = {false, false};
    Vec3 palmNormal[2] = {{0, -1, 0}, {0, -1, 0}};   // world direction out of the palm
    Vec3 fingerDir[2] = {{0, 0, 1}, {0, 0, 1}};      // world direction the fingers point
    float orientWeight[2] = {1.f, 1.f};
    float fingerCurl[2] = {0.25f, 0.25f};           // 0 flat .. 1 fist (relaxed hands curl a little)
    float fingerSpread[2] = {0.f, 0.f};
    float thumbCurl[2] = {0.2f, 0.2f};
    float indexCurl[2] = {-1.f, -1.f};             // >= 0: index finger on its own (pointing)
};

// Builds the full pose for this frame (resets to bind first). Call before soft tissue/limb collision.
void ApplyPosture(Character& ch, const PostureParams& p);

const char* MotionName(TestMotion m);

} // namespace eon
