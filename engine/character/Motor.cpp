#include "Motor.h"

#include <algorithm>
#include <cmath>

namespace eon {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kPalm = 0.08f;

float WrapAngle(float a) {
    while (a > kPi) a -= 2.f * kPi;
    while (a < -kPi) a += 2.f * kPi;
    return a;
}

float Smooth(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

Vec3 Flat(const Vec3& v) { return {v.x, 0.f, v.z}; }

} // namespace

void Motor::Teleport(Character& ch, const Vec3& pos, float yaw) {
    pos_ = pos;
    yaw_ = yaw;
    vel_ = {};
    ch.placement.t = pos_ + Vec3{0, ch.floorOffset, 0};
    ch.placement.r = Quat::AxisAngle({0, 1, 0}, yaw_);
    for (int i = 0; i < 2; ++i) {
        footPlant_[i] = ch.BindPos(ch.Rig().foot[i]);
        footSwing_[i] = false;
    }
    init_ = true;
    generation_ = -1;   // re-place physical bodies
}

void Motor::Init(Character& ch, World& world) {
    pos_ = {ch.placement.t.x, ch.placement.t.y - ch.floorOffset, ch.placement.t.z};
    const Vec3 f = ch.placement.r.Rotate({0, 0, 1});
    yaw_ = std::atan2(f.x, f.z);
    Teleport(ch, pos_, yaw_);
    EnsureBodies(ch, world);
}

void Motor::EnsureBodies(Character& ch, World& world) {
    if (generation_ == world.Generation()) return;
    generation_ = world.Generation();

    // Torso: moved by her legs (kinematic) so she shoves light things and can't be pushed by them
    BodyDesc t;
    t.shape = Shape::Capsule;
    t.radius = 0.16f;
    t.halfHeight = 0.42f;
    t.pos = pos_ + Vec3{0, 0.95f, 0};
    t.kinematic = true;
    t.mass = 60.f;
    torso_ = world.AddAgentBody(t);

    // Hands: real little bodies driven by her arm muscles; they get stopped by walls and press buttons
    for (int i = 0; i < 2; ++i) {
        BodyDesc h;
        h.shape = Shape::Capsule;
        h.radius = 0.035f;
        h.halfHeight = 0.03f;
        h.mass = 0.45f;
        h.friction = 0.9f;
        h.restitution = 0.f;
        h.linearDamping = 1.f;
        h.angularDamping = 6.f;
        h.gravityFactor = 0.f;        // the arm holds the hand up; weight of held things is felt separately
        h.fastMoving = true;
        const int hb = ch.Rig().hand[i];
        h.pos = hb >= 0 ? ch.BonePos(hb) : pos_ + Vec3{i == 0 ? 0.2f : -0.2f, 0.8f, 0};
        hand_[i] = world.AddAgentBody(h);
        restHand_[i] = h.pos;
        joint_[i] = -1;
        held_[i] = -1;
    }
}

void Motor::PrePhysics(Character& ch, World& world, const MotorIntent& in, float dt) {
    EnsureBodies(ch, world);
    StepLocomotion(ch, world, in, dt);
    UpdateHands(ch, world, in, dt);
}

void Motor::StepLocomotion(Character& ch, World& world, const MotorIntent& in, float dt) {
    fb_.bumped = false;
    Vec3 want = Flat(in.moveDir);
    const float wl = want.Length();
    if (wl > 1.f) want = want * (1.f / wl);
    want = want * maxSpeed;

    // accelerate like a body, not a cursor
    Vec3 dv = want - vel_;
    const float maxDv = accel * dt;
    if (dv.Length() > maxDv) dv = dv.Normalized() * maxDv;
    vel_ += dv;

    // walls and heavy things stop her; light things get pushed by her torso
    const float speed = vel_.Length();
    if (speed > 1e-4f) {
        const Vec3 dir = vel_ * (1.f / speed);
        for (float h : {0.25f, 0.6f, 1.0f, 1.45f}) {
            RayHit hit;
            if (world.RayCast(pos_ + Vec3{0, h, 0}, dir, speed * dt + bodyRadius, hit, true)) {
                const float m = world.BodyMass(hit.body);
                if (m <= 0.f || m > 25.f) {
                    const float into = vel_.Dot(dir);
                    if (into > 0.f) vel_ -= dir * into;
                    fb_.bumped = true;
                    break;
                }
            }
        }
    }
    pos_ += vel_ * dt;

    // stand on whatever is under her (floor, a low step)
    RayHit ground;
    if (world.RayCast(pos_ + Vec3{0, 0.4f, 0}, {0, -1, 0}, 1.4f, ground, true)) {
        const float h = ground.point.y;
        if (h - pos_.y < 0.3f) pos_.y += (h - pos_.y) * std::clamp(dt * 12.f, 0.f, 1.f);
    }

    // turn: toward the face target, else toward where she walks
    float targetYaw = yaw_;
    if (in.hasFace) targetYaw = in.faceYaw;
    else if (Flat(vel_).Length() > 0.15f) targetYaw = std::atan2(vel_.x, vel_.z);
    const float d = WrapAngle(targetYaw - yaw_);
    yaw_ = WrapAngle(yaw_ + std::clamp(d, -turnRate * dt, turnRate * dt));

    ch.placement.t = pos_ + Vec3{0, ch.floorOffset, 0};
    ch.placement.r = Quat::AxisAngle({0, 1, 0}, yaw_);
    world.SetKinematicTarget(torso_, pos_ + Vec3{0, 0.95f, 0}, ch.placement.r);

    fb_.speed = Flat(vel_).Length();
}

void Motor::UpdateHands(Character& ch, World& world, const MotorIntent& in, float dt) {
    const RigMap& r = ch.Rig();
    for (int i = 0; i < 2; ++i) {
        if (hand_[i] < 0) continue;
        const HandIntent& h = in.hands[i];
        const Vec3 hp = world.BodyPosition(hand_[i]);
        reaching_[i] = h.reach;

        // where the hand should go, limited to what the arm can reach
        Vec3 target = h.reach ? h.target : restHand_[i];
        if (r.upperarm[i] >= 0 && r.lowerarm[i] >= 0 && r.hand[i] >= 0) {
            const Vec3 sh = ch.BonePos(r.upperarm[i]);
            // arm + palm: the physical hand is the palm, ~8 cm past the wrist bone
            const float len = (Distance(sh, ch.BonePos(r.lowerarm[i])) + Distance(ch.BonePos(r.lowerarm[i]), ch.BonePos(r.hand[i]))) * 0.98f + kPalm;
            const Vec3 d = target - sh;
            deficit_[i] = h.reach ? d.Length() - len : 0.f;
            if (d.Length() > len) target = sh + d.Normalized() * len;
        }
        const float strength = h.reach ? std::max(10.f, armStrength * std::clamp(h.effort, 0.f, 1.f)) : 25.f;
        // holding something heavy: the muscle has to carry its weight too
        const float carry = held_[i] >= 0 ? world.BodyMass(held_[i]) * 9.81f : 0.f;
        world.SetDrive(hand_[i], target + Vec3{0, held_[i] >= 0 ? 0.f : 0.f, 0}, strength + carry, h.reach ? 7.f : 5.f, 1.f);

        // fingers: close on whatever the palm is touching
        if (h.grip && joint_[i] < 0) {
            const Vec3 f = ch.placement.r.Rotate({0, 0, 1});
            const Vec3 side = Vec3{0, 1, 0}.Cross(f);
            Vec3 dirs[7] = {(target - hp).Normalized(f), {0, -1, 0}, f, side, side * -1.f, (f + Vec3{0, -1, 0}).Normalized(),
                            (hp - ch.BonePos(r.lowerarm[i] >= 0 ? r.lowerarm[i] : 0)).Normalized({0, -1, 0})};
            // fingers wrap around what's under the palm: prefer something movable over the table it sits on
            RayHit best;
            bool found = false, bestDynamic = false;
            for (const Vec3& d : dirs) {
                RayHit hit;
                if (!world.RayCast(hp, d, 0.08f, hit, true)) continue;
                const bool dyn = world.BodyMass(hit.body) > 0.f;
                if (!found || (dyn && !bestDynamic) || (dyn == bestDynamic && hit.dist < best.dist)) {
                    best = hit;
                    bestDynamic = dyn;
                    found = true;
                }
            }
            if (found) {
                joint_[i] = world.Attach(hand_[i], best.body, best.point);
                held_[i] = best.body;
                slip_[i] = 0;
            }
        } else if (!h.grip && joint_[i] >= 0) {
            world.Detach(joint_[i]);
            joint_[i] = held_[i] = -1;
        }
        if (joint_[i] >= 0) {
            slip_[i] = world.JointForce(joint_[i]) > gripStrength ? slip_[i] + 1 : 0;
            if (slip_[i] > 8) { world.Detach(joint_[i]); joint_[i] = held_[i] = -1; }
        }

        fb_.holding[i] = joint_[i] >= 0;
        fb_.heldMass[i] = held_[i] >= 0 ? world.BodyMass(held_[i]) : 0.f;
        const float lag = Distance(hp, target);
        fb_.handBlocked[i] = h.reach && lag > 0.06f && world.BodyVelocity(hand_[i]).Length() < 0.05f;
        fb_.handForce[i] = std::min(strength + carry, lag * 600.f + carry);
    }
    (void)dt;
}

void Motor::PostPhysics(Character& ch, World& world, PostureParams base, float dt) {
    const RigMap& r = ch.Rig();
    const float speed = fb_.speed;
    const bool moving = speed > 0.05f;

    // ---- gait: feet planted in the world, swing foot lifted and placed ahead ----
    const float freq = 0.85f + 0.55f * speed;    // steps per second per leg cycle
    if (moving || footSwing_[0] || footSwing_[1]) gaitPhase_ += freq * dt;
    const Vec3 fwd = ch.placement.r.Rotate({0, 0, 1});
    const Vec3 lead = vel_ * (0.32f / freq);
    for (int i = 0; i < 2; ++i) {
        if (r.foot[i] < 0) continue;
        const Vec3 rest = ch.BindPos(r.foot[i]);
        const float ph = std::fmod(gaitPhase_ + 0.5f * i, 1.f);
        const bool wantSwing = moving ? ph < 0.4f : Distance(Flat(footPlant_[i]), Flat(rest)) > 0.07f && !footSwing_[1 - i];
        if (wantSwing && !footSwing_[i]) {
            footSwing_[i] = true;
            footFrom_[i] = footPlant_[i];
            if (!moving) gaitPhase_ = std::floor(gaitPhase_) + (i == 0 ? 0.f : 0.5f);
        }
        if (footSwing_[i]) {
            const float s = moving ? ph / 0.4f : std::clamp((std::fmod(gaitPhase_ + 0.5f * i, 1.f)) / 0.4f, 0.f, 1.f);
            const Vec3 land = rest + (moving ? lead : Vec3{});
            Vec3 p = footFrom_[i] + (land - footFrom_[i]) * Smooth(s);
            p.y = rest.y + 0.075f * std::sin(kPi * std::clamp(s, 0.f, 1.f));
            footPlant_[i] = p;
            if (s >= 0.98f || (moving && ph >= 0.4f)) {
                footSwing_[i] = false;
                footPlant_[i] = Vec3{land.x, rest.y, land.z};
            }
        } else {
            footPlant_[i].y = rest.y;
        }
        base.footTarget[i] = footPlant_[i];
    }
    base.footOverride = r.foot[0] >= 0 && r.foot[1] >= 0;

    // hips: lower a touch and bob with each step; sway over the stance leg
    if (moving) {
        const float bob = std::cos(gaitPhase_ * 4.f * kPi);
        const Vec3 left = Vec3{0, 1, 0}.Cross(fwd).Normalized({1, 0, 0});
        base.pelvisOffset = Vec3{0, -0.025f - 0.012f * bob, 0} + left * (0.018f * std::sin(gaitPhase_ * 2.f * kPi));
        for (int i = 0; i < 2; ++i) base.armSwing[i] = 0.16f * (speed / maxSpeed) * std::sin(gaitPhase_ * 2.f * kPi + (i == 0 ? 0.f : kPi));
    }

    // arms follow her physical hands
    for (int i = 0; i < 2; ++i) {
        if (hand_[i] < 0) continue;
        base.handOverride[i] = reaching_[i] || joint_[i] >= 0;
        // the wrist sits a palm-length behind the physical hand, back toward the elbow
        const Vec3 hp = world.BodyPosition(hand_[i]);
        const Vec3 el = r.lowerarm[i] >= 0 ? ch.BonePos(r.lowerarm[i]) : hp;
        base.handTarget[i] = hp - (hp - el).Normalized({0, -1, 0}) * kPalm;
    }

    // lean over / twist toward a reach that the arm alone can't make (a person bends at the waist)
    float wantLean = 0.f, wantTwist = 0.f;
    for (int i = 0; i < 2; ++i) {
        if (!reaching_[i]) continue;
        wantLean = std::max(wantLean, lean_ + deficit_[i] * 4.f);
        const Vec3 d = Flat(world.BodyPosition(hand_[i]) - pos_);
        if (d.Length() > 0.05f) {
            const Vec3 f = ch.placement.r.Rotate({0, 0, 1});
            wantTwist = std::clamp(std::atan2(f.Cross(d.Normalized()).y, f.Dot(d.Normalized())) * 0.4f, -0.5f, 0.5f);
        }
    }
    wantLean = std::clamp(wantLean, 0.f, 1.3f);
    lean_ += (wantLean - lean_) * std::clamp(dt * 5.f, 0.f, 1.f);
    twist_ += (wantTwist - twist_) * std::clamp(dt * 3.f, 0.f, 1.f);
    base.lean = lean_;
    base.leanTwist = twist_;

    ApplyPosture(ch, base);

    // where relaxed hands hang this frame (next frame's muscle target) = the posed hand position
    for (int i = 0; i < 2; ++i) {
        if (r.hand[i] < 0 || base.handOverride[i]) continue;
        const Vec3 wrist = ch.BonePos(r.hand[i]);
        const Vec3 el = r.lowerarm[i] >= 0 ? ch.BonePos(r.lowerarm[i]) : wrist;
        restHand_[i] = wrist + (wrist - el).Normalized({0, -1, 0}) * kPalm;
    }
    // when not reaching, keep the physical hand glued to the animated one (it still collides on the way)
    fb_.exertion = std::clamp(0.05f + 0.25f * (speed / maxSpeed) * (speed / maxSpeed) +
                              0.4f * std::max(fb_.handForce[0], fb_.handForce[1]) / armStrength, 0.f, 1.f);
}

} // namespace eon
