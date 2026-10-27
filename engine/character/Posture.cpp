#include "Posture.h"

#include <algorithm>
#include <cmath>

namespace eon {

namespace {
constexpr float kPi = 3.14159265358979f;
}

const char* MotionName(TestMotion m) {
    switch (m) {
    case TestMotion::Idle: return "idle";
    case TestMotion::Bounce: return "bounce";
    case TestMotion::WalkInPlace: return "walk in place";
    case TestMotion::HandsOnChest: return "hands on chest";
    case TestMotion::HandsOnHips: return "hands on hips";
    case TestMotion::ArmsCrossed: return "arms crossed";
    case TestMotion::Turn: return "turn";
    }
    return "?";
}

void ApplyPosture(Character& ch, const PostureParams& p) {
    const RigMap& r = ch.Rig();
    const float t = p.time;

    ch.ResetPose();

    // Whole-body motion through the pelvis (soft tissue reacts to all of this)
    if (r.pelvis >= 0) {
        Transform& pel = ch.Local()[r.pelvis];
        const int parent = ch.Asset()->bones[r.pelvis].parent;
        const Quat toLocal = parent >= 0 ? ch.World()[parent].r.Conjugate() : Quat{};
        Vec3 offsetW{};
        Quat turnW{};
        switch (p.motion) {
        case TestMotion::Bounce: {
            // jump-like: quick rise, hang, drop and land
            // bouncing on the knees: drop, then drive back up (feet stay planted)
            const float ph = std::fmod(t * 1.6f, 1.f);
            offsetW.y = -0.07f * std::pow(std::sin(ph * kPi), 2.f);
            break;
        }
        case TestMotion::WalkInPlace: {
            offsetW.y = -0.02f - 0.015f * std::fabs(std::sin(t * 2.f * kPi * 0.9f));
            offsetW = offsetW + Vec3{0.02f * std::sin(t * kPi * 0.9f), 0, 0};
            break;
        }
        case TestMotion::Turn:
            turnW = Quat::AxisAngle({0, 1, 0}, 0.6f * std::sin(t * 2.f * kPi * 0.5f));
            break;
        default: break;
        }
        const Vec3 d = toLocal.Rotate(offsetW);
        const Vec3 ps = parent >= 0 ? ch.World()[parent].s : Vec3{1, 1, 1};
        pel.t += Vec3{d.x / ps.x, d.y / ps.y, d.z / ps.z};
        ch.ComputeWorld();
        if (p.motion == TestMotion::Turn) ch.RotateBoneWorld(r.pelvis, turnW);
    }

    // Breathing: small chest rise
    const float breath = 0.5f + 0.5f * std::sin(t * 2.f * kPi * p.breathRate);
    const int top = r.SpineTop();
    Vec3 fwd = ch.placement.r.Rotate({0, 0, 1});
    Vec3 left{1, 0, 0};
    if (r.thigh[0] >= 0 && r.thigh[1] >= 0) left = (ch.BonePos(r.thigh[0]) - ch.BonePos(r.thigh[1])).Normalized({1, 0, 0});
    if (top >= 0) ch.RotateBoneWorld(top, Quat::AxisAngle(left, -0.02f * breath));

    // Legs stay planted (or step) with IK
    for (int i = 0; i < 2; ++i) {
        if (r.thigh[i] < 0 || r.calf[i] < 0 || r.foot[i] < 0) continue;
        const Vec3 knee = ch.BonePos(r.calf[i]);
        // bind foot position (pelvis offset shouldn't drag the feet)
        Vec3 foot = ch.placement.Apply(Quat::AxisAngle({0, 1, 0}, ch.modelYaw).Rotate(
            Vec3{ch.Asset()->bones[r.foot[i]].bindWorld.m[12], ch.Asset()->bones[r.foot[i]].bindWorld.m[13],
                 ch.Asset()->bones[r.foot[i]].bindWorld.m[14]}));
        if (p.motion == TestMotion::WalkInPlace) {
            const float ph = t * 2.f * kPi * 0.9f + (i == 0 ? 0.f : kPi);
            foot.y += 0.08f * std::max(0.f, std::sin(ph));
        }
        ch.SolveTwoBone(r.thigh[i], r.calf[i], r.foot[i], foot, knee + fwd * 0.5f);
    }

    // Arms: relaxed at the sides, or placed on the body to test contact
    for (int i = 0; i < 2; ++i) {
        const int a = r.upperarm[i], b = r.lowerarm[i], c = r.hand[i];
        if (a < 0 || b < 0 || c < 0) continue;
        const float sideSign = i == 0 ? 1.f : -1.f;
        const Vec3 sh = ch.BonePos(a);
        const float armLen = Distance(sh, ch.BonePos(b)) + Distance(ch.BonePos(b), ch.BonePos(c));
        Vec3 target, pole;
        const float sway = p.motion == TestMotion::WalkInPlace ? 0.12f * std::sin(t * 2.f * kPi * 0.9f + (i == 0 ? kPi : 0.f)) : 0.f;
        switch (p.motion) {
        case TestMotion::HandsOnChest: {
            const Vec3 chest = top >= 0 ? ch.BonePos(top) : sh;
            // aim slightly into the upper chest; limb collision stops the palm on the skin
            target = chest + fwd * 0.04f + left * (sideSign * 0.05f) + Vec3{0, 0.07f, 0};
            pole = sh + left * (sideSign * 0.3f) + Vec3{0, -0.4f, 0};
            break;
        }
        case TestMotion::HandsOnHips: {
            const Vec3 hip = r.pelvis >= 0 ? ch.BonePos(r.pelvis) : sh;
            target = hip + left * (sideSign * 0.16f) + Vec3{0, 0.06f, 0};
            pole = sh + left * (sideSign * 0.6f) - fwd * 0.3f;
            break;
        }
        case TestMotion::ArmsCrossed: {
            const Vec3 chest = top >= 0 ? ch.BonePos(top) : sh;
            target = chest + fwd * 0.14f - left * (sideSign * 0.12f) + Vec3{0, -0.12f, 0};
            pole = sh + left * (sideSign * 0.4f) + Vec3{0, -0.3f, 0};
            break;
        }
        default:
            target = sh + Vec3{0, -armLen * 0.97f, 0} + left * (sideSign * 0.05f) + fwd * (0.03f + sway);
            pole = sh + Vec3{0, -0.3f, 0} - fwd * 0.5f + left * (sideSign * 0.1f);
            break;
        }
        // relaxed arms: hands follow the forearm; contact poses keep the palm orientation
        const bool relaxed = p.motion != TestMotion::HandsOnChest && p.motion != TestMotion::HandsOnHips && p.motion != TestMotion::ArmsCrossed;
        ch.SolveTwoBone(a, b, c, target, pole, 1.f, !relaxed);
    }
}

} // namespace eon
