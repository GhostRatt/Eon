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
    // feet keep their own orientation (flat on the floor) whatever the hips and back do above them
    Quat footRot[2];
    for (int i = 0; i < 2; ++i) footRot[i] = r.foot[i] >= 0 ? ch.World()[r.foot[i]].r : Quat{};

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
        offsetW += p.pelvisOffset;
        const Vec3 d = toLocal.Rotate(offsetW);
        const Vec3 ps = parent >= 0 ? ch.World()[parent].s : Vec3{1, 1, 1};
        pel.t += Vec3{d.x / ps.x, d.y / ps.y, d.z / ps.z};
        ch.ComputeWorld();
        if (p.motion == TestMotion::Turn) ch.RotateBoneWorld(r.pelvis, turnW);
        // whole-body tilt about the hip joint (pelvis bone): lying, crawling
        if (std::fabs(p.bodyTilt.w) < 0.99999f) ch.RotateBoneWorld(r.pelvis, p.bodyTilt);
    }

    // Breathing: small chest rise
    const float phase = p.breathPhase >= 0.f ? p.breathPhase : t * p.breathRate;
    const float breath = (0.5f + 0.5f * std::sin(phase * 2.f * kPi)) * (0.4f + 1.2f * p.breathDepth);
    const int top = r.SpineTop();
    Vec3 fwd = ch.placement.r.Rotate({0, 0, 1});
    Vec3 left{1, 0, 0};
    if (r.thigh[0] >= 0 && r.thigh[1] >= 0) left = (ch.BonePos(r.thigh[0]) - ch.BonePos(r.thigh[1])).Normalized({1, 0, 0});
    if (top >= 0) ch.RotateBoneWorld(top, Quat::AxisAngle(left, -0.02f * breath));
    // Bending over / reaching: the spine folds forward (most at the lower back) and twists toward the hand
    if (std::fabs(p.lean) > 1e-3f || std::fabs(p.leanTwist) > 1e-3f) {
        // most of a bend comes from the hips (the legs are re-planted by IK below), the rest from the back
        const int chain[4] = {r.pelvis, r.spine[0], r.spine[1], r.spine[2]};
        const float share[4] = {0.45f, 0.25f, 0.18f, 0.12f};
        for (int k = 0; k < 4; ++k) {
            if (chain[k] < 0) continue;
            ch.RotateBoneWorld(chain[k], Quat::AxisAngle(left, p.lean * share[k]) * Quat::AxisAngle({0, 1, 0}, p.leanTwist * share[k]));
        }
    }
    // Shivering: fast small tremor through the trunk and shoulders, arms pulled in a little
    if (p.shiver > 0.01f) {
        const float tr = p.shiver * 0.012f;
        const float n1 = std::sin(t * 2.f * kPi * 9.3f) + 0.6f * std::sin(t * 2.f * kPi * 13.7f + 1.3f);
        const float n2 = std::sin(t * 2.f * kPi * 11.1f + 0.7f);
        if (r.spine[0] >= 0) ch.RotateBoneWorld(r.spine[0], Quat::AxisAngle(fwd, tr * n1));
        if (top >= 0) ch.RotateBoneWorld(top, Quat::AxisAngle(left, tr * n2));
        for (int i = 0; i < 2; ++i)
            if (r.clavicle[i] >= 0) ch.RotateBoneWorld(r.clavicle[i], Quat::AxisAngle(fwd, (i == 0 ? 1.f : -1.f) * (0.08f * p.shiver + tr * n2)));
    }

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
        if (p.footOverride) foot = p.footTarget[i];
        const Vec3 pole = p.kneePoleOverride ? p.kneePole[i] : knee + fwd * 0.5f;
        ch.SolveTwoBone(r.thigh[i], r.calf[i], r.foot[i], foot, pole, 1.f, false);
        if (!p.footFollow) {
            ch.RotateBoneWorld(r.foot[i], (footRot[i] * ch.World()[r.foot[i]].r.Conjugate()).Normalized());
        }
        if (p.footFollow) {
            // feet relax/point; the floor pushes the toes up (lying face down the tops of the feet lie flat)
            const Vec3 kneeAxis = (ch.BonePos(r.calf[i]) - ch.BonePos(r.foot[i])).Cross(pole - ch.BonePos(r.calf[i])).Normalized(left);
            if (std::fabs(p.footPitch[i]) > 1e-3f) ch.RotateBoneWorld(r.foot[i], Quat::AxisAngle(kneeAxis, p.footPitch[i]));
            // the floor pushes the foot: turn it (point or flex at the ankle) until no part of it is under the floor
            const float want = p.floorY + 0.004f;
            float lo = ch.LowestFootPoint(i);
            for (int it = 0; it < 10 && lo < want; ++it) {
                const float stepA = std::clamp((want - lo) * 4.f, 0.03f, 0.25f);
                ch.RotateBoneWorld(r.foot[i], Quat::AxisAngle(kneeAxis, stepA));
                const float a = ch.LowestFootPoint(i);
                ch.RotateBoneWorld(r.foot[i], Quat::AxisAngle(kneeAxis, -2.f * stepA));
                const float b = ch.LowestFootPoint(i);
                if (a >= b) { ch.RotateBoneWorld(r.foot[i], Quat::AxisAngle(kneeAxis, 2.f * stepA)); lo = a; }
                else lo = b;
            }
        }
        // flat on whatever is under the foot (slopes, steps): tilt the foot to the ground normal
        if (!p.footFollow) {
            const Vec3 n = p.footNormal[i].Normalized({0, 1, 0});
            if (n.y < 0.9999f) ch.RotateBoneWorld(r.foot[i], Quat::FromTo({0, 1, 0}, n));
        }
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
            target = sh + Vec3{0, -armLen * 0.97f, 0} + left * (sideSign * 0.05f) + fwd * (0.03f + sway + p.armSwing[i]);
            pole = sh + Vec3{0, -0.3f, 0} - fwd * 0.5f + left * (sideSign * 0.1f);
            break;
        }
        if (p.handOverride[i]) {
            target = p.handTarget[i];
            // elbow bends down and out, like a person reaching
            pole = sh + Vec3{0, -0.45f, 0} + left * (sideSign * 0.35f) - fwd * 0.2f;
            if (p.elbowPoleOverride[i]) pole = p.elbowPole[i];
        }
        // relaxed arms: hands follow the forearm; contact poses keep the palm orientation
        const bool relaxed = p.motion != TestMotion::HandsOnChest && p.motion != TestMotion::HandsOnHips && p.motion != TestMotion::ArmsCrossed;
        ch.SolveTwoBone(a, b, c, target, pole, 1.f, !relaxed && !p.handOverride[i]);
        if (p.palmDown[i] || p.orientHand[i]) {
            // planted: palm flat on the surface below, fingers ahead; otherwise whatever the motor asked for
            Vec3 palm = p.palmDown[i] ? Vec3{0, -1, 0} : p.palmNormal[i];
            Vec3 fingers = p.fingerDir[i];
            if (p.palmDown[i] && !p.orientHand[i]) {
                const Vec3 handDir = (ch.BonePos(c) - ch.BonePos(b)).Normalized({0, -1, 0});
                fingers = Vec3{handDir.x, 0, handDir.z}.Normalized(fwd);
            }
            ch.OrientHand(i, palm, fingers, p.orientWeight[i]);
        }
        ch.PoseFingers(i, p.fingerCurl[i], p.fingerSpread[i], p.thumbCurl[i], p.indexCurl[i]);
    }

    // Head turns toward what she looks at (neck takes 40%, head the rest, limited like a real neck)
    if (p.hasLook && r.head >= 0) {
        const Vec3 hp = ch.BonePos(r.head);
        Vec3 to = (p.lookAt - hp).Normalized(fwd);
        const Vec3 flatTo = Vec3{to.x, 0, to.z}.Normalized(fwd);
        float yaw = std::atan2(fwd.Cross(flatTo).y, fwd.Dot(flatTo));
        yaw = std::clamp(yaw, -1.2f, 1.2f);
        const float pitch = std::clamp(std::asin(std::clamp(to.y, -1.f, 1.f)), -0.7f, 0.6f);
        const Quat qYaw = Quat::AxisAngle({0, 1, 0}, yaw);
        const Vec3 side = qYaw.Rotate(left);
        const Quat q = Quat::AxisAngle(side, -pitch) * qYaw;
        auto part = [](const Quat& qq, float k) {
            const Quat id{};
            const float d = qq.w < 0 ? -1.f : 1.f;
            return Quat{qq.x * d * k, qq.y * d * k, qq.z * d * k, id.w + (qq.w * d - 1.f) * k}.Normalized();
        };
        if (r.neck >= 0) ch.RotateBoneWorld(r.neck, part(q, 0.4f));
        ch.RotateBoneWorld(r.head, part(q, 0.6f));
    }
    if (std::fabs(p.headPitch) > 1e-3f && r.head >= 0) {
        if (r.neck >= 0) ch.RotateBoneWorld(r.neck, Quat::AxisAngle(left, -p.headPitch * 0.5f));
        ch.RotateBoneWorld(r.head, Quat::AxisAngle(left, -p.headPitch * 0.5f));
    }
    if (std::fabs(p.headRoll) > 1e-3f && r.head >= 0) {
        if (r.neck >= 0) ch.RotateBoneWorld(r.neck, Quat::AxisAngle(p.headRollAxis, p.headRoll * 0.5f));
        ch.RotateBoneWorld(r.head, Quat::AxisAngle(p.headRollAxis, p.headRoll * 0.5f));
    }
}

} // namespace eon
