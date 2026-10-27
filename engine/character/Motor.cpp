#include "Motor.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

const char* StanceName(Stance s) {
    switch (s) {
    case Stance::Stand: return "stand";
    case Stance::Crouch: return "crouch";
    case Stance::Kneel: return "kneel";
    case Stance::Sit: return "sit";
    case Stance::Crawl: return "crawl";
    case Stance::Prone: return "prone";
    case Stance::LieBack: return "lie on back";
    default: return "?";
    }
}

namespace {

// Which body positions you can go straight between (a person doesn't go from standing to lying in one move)
const std::vector<std::vector<Stance>>& StanceGraph() {
    static const std::vector<std::vector<Stance>> g = {
        /*Stand */ {Stance::Crouch},
        /*Crouch*/ {Stance::Stand, Stance::Kneel, Stance::Sit},
        /*Kneel */ {Stance::Crouch, Stance::Crawl, Stance::Sit},
        /*Sit   */ {Stance::Crouch, Stance::Kneel, Stance::LieBack},
        /*Crawl */ {Stance::Kneel, Stance::Prone},
        /*Prone */ {Stance::Crawl},
        /*Lie   */ {Stance::Sit},
    };
    return g;
}

Stance NextHop(Stance from, Stance to) {
    if (from == to) return to;
    const auto& g = StanceGraph();
    const int n = static_cast<int>(Stance::Count);
    std::vector<int> prev(n, -1);
    std::vector<int> q = {static_cast<int>(from)};
    prev[static_cast<int>(from)] = static_cast<int>(from);
    for (size_t k = 0; k < q.size(); ++k) {
        for (Stance nb : g[q[k]]) {
            const int v = static_cast<int>(nb);
            if (prev[v] >= 0) continue;
            prev[v] = q[k];
            q.push_back(v);
        }
    }
    int cur = static_cast<int>(to);
    if (prev[cur] < 0) return from;
    while (prev[cur] != static_cast<int>(from)) cur = prev[cur];
    return static_cast<Stance>(cur);
}

float HopDuration(Stance a, Stance b) {
    auto is = [&](Stance x, Stance y) { return (a == x && b == y) || (a == y && b == x); };
    if (is(Stance::Stand, Stance::Crouch)) return 0.6f;
    if (is(Stance::Crouch, Stance::Kneel)) return 0.7f;
    if (is(Stance::Kneel, Stance::Crawl)) return 0.8f;
    if (is(Stance::Crawl, Stance::Prone)) return 1.1f;
    if (is(Stance::Kneel, Stance::Sit)) return 1.1f;
    if (is(Stance::Crouch, Stance::Sit)) return 1.0f;
    if (is(Stance::Sit, Stance::LieBack)) return 1.2f;
    return 0.8f;
}

Quat Nlerp(const Quat& a, const Quat& b, float t) {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0 ? -1.f : 1.f;
    return Quat{a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t}.Normalized();
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

} // namespace

float Motor::StanceSpeed(Stance s) const {
    switch (s) {
    case Stance::Stand: return maxSpeed;
    case Stance::Crouch: return 0.5f;
    case Stance::Crawl: return 0.45f;
    case Stance::Prone: return 0.18f;
    default: return 0.f;
    }
}

float Motor::GroundAt(World& world, const Vec3& p, Vec3* normal) const {
    RayHit g;
    if (world.RayCast(Vec3{p.x, p.y + 0.6f, p.z}, {0, -1, 0}, 2.0f, g, true)) {
        if (normal) *normal = world.SurfaceNormal(g.body, g.point);
        return g.point.y;
    }
    if (normal) *normal = {0, 1, 0};
    return pos_.y;
}

void Motor::Teleport(Character& ch, const Vec3& pos, float yaw) {
    pos_ = pos;
    yaw_ = yaw;
    vel_ = {};
    ch.placement.t = pos_ + Vec3{0, ch.floorOffset, 0};
    ch.placement.r = Quat::AxisAngle({0, 1, 0}, yaw_);
    ch.ResetPose();
    const RigMap& r = ch.Rig();
    for (int i = 0; i < 2; ++i) {
        footPlant_[i] = ch.BindPos(r.foot[i]);
        footSwing_[i] = false;
    }
    // body measurements from the bind pose
    auto dist = [&](int a, int b) { return a >= 0 && b >= 0 ? Distance(ch.BindPos(a), ch.BindPos(b)) : 0.f; };
    if (r.thigh[0] >= 0) {
        dims_.thigh = dist(r.thigh[0], r.calf[0]);
        dims_.shin = dist(r.calf[0], r.foot[0]);
        dims_.hipH = ch.BindPos(r.pelvis).y - pos_.y;
        dims_.ankleH = ch.BindPos(r.foot[0]).y - pos_.y;
    }
    if (r.upperarm[0] >= 0) {
        dims_.torso = dist(r.pelvis, r.upperarm[0]);
        dims_.arm = dist(r.upperarm[0], r.lowerarm[0]) + dist(r.lowerarm[0], r.hand[0]);
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
    t.halfHeight = 0.3f;
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

void Motor::UpdateStance(const MotorIntent& in, float dt) {
    if (climbing_) return;
    if (trans_ >= 1.f && stance_ != in.stance) {
        next_ = NextHop(stance_, in.stance);
        if (next_ != stance_) {
            transDur_ = HopDuration(stance_, next_);
            trans_ = 0.f;
        }
    }
    if (trans_ < 1.f) {
        trans_ = std::min(1.f, trans_ + dt / transDur_);
        if (trans_ >= 1.f) stance_ = next_;
    }
    fb_.stance = stance_;
    fb_.inTransition = trans_ < 1.f;
}

void Motor::PrePhysics(Character& ch, World& world, const MotorIntent& in, float dt) {
    EnsureBodies(ch, world);
    UpdateStance(in, dt);
    if (in.climb && !climbing_ && stance_ == Stance::Stand && trans_ >= 1.f) StartClimb(ch, world, in);
    StepLocomotion(ch, world, in, dt);
    UpdateHands(ch, world, in, dt);
}

void Motor::StartClimb(Character& ch, World& world, const MotorIntent& in) {
    Vec3 dir = Flat(in.moveDir);
    if (dir.Length() < 0.1f) dir = ch.placement.r.Rotate({0, 0, 1});
    dir = dir.Normalized();
    // up: something in front at knee height whose top is within reach
    RayHit wall;
    if (world.RayCast(pos_ + Vec3{0, 0.3f, 0}, dir, 0.7f, wall, true) && world.BodyMass(wall.body) <= 0.f) {
        const Vec3 probe = Vec3{wall.point.x, pos_.y, wall.point.z} + dir * 0.2f;
        RayHit top;
        if (world.RayCast(probe + Vec3{0, 1.8f, 0}, {0, -1, 0}, 1.8f, top, true)) {
            const float h = top.point.y - pos_.y;
            RayHit head;
            const bool room = !world.RayCast(top.point + Vec3{0, 0.05f, 0}, {0, 1, 0}, 1.1f, head, true);
            if (h > 0.3f && h < 1.35f && room) {
                climbing_ = true;
                climbDown_ = false;
                climbT_ = 0.f;
                climbDur_ = 1.2f + 1.4f * h;
                climbDir_ = dir;
                climbFrom_ = pos_;
                climbEdge_ = Vec3{wall.point.x, top.point.y, wall.point.z};
                climbTopY_ = top.point.y;
                climbTo_ = Vec3{climbEdge_.x, top.point.y, climbEdge_.z} + dir * 0.35f;
                return;
            }
        }
    }
    // down: a drop just ahead that she won't walk off
    const Vec3 ahead = pos_ + dir * 0.45f;
    const float g = GroundAt(world, Vec3{ahead.x, pos_.y, ahead.z});
    const float drop = pos_.y - g;
    if (drop > 0.3f && drop < 1.35f) {
        climbing_ = true;
        climbDown_ = true;
        climbT_ = 0.f;
        climbDur_ = 1.2f + 1.2f * drop;
        climbDir_ = dir;
        climbFrom_ = pos_;
        climbEdge_ = pos_ + dir * 0.25f;
        climbTopY_ = pos_.y;
        climbTo_ = Vec3{ahead.x, g, ahead.z} + dir * 0.15f;
    }
}

void Motor::StepLocomotion(Character& ch, World& world, const MotorIntent& in, float dt) {
    fb_.bumped = false;
    fb_.atEdge = false;
    fb_.climbing = climbing_;

    if (climbing_) {
        // body path: from the floor, over the edge, onto the top (or the reverse going down)
        climbT_ += dt / climbDur_;
        const float c = std::min(1.f, climbT_);
        const float up = Smooth((c - 0.15f) / 0.5f);
        const float over = Smooth((c - 0.35f) / 0.55f);
        if (!climbDown_) {
            pos_ = Lerp(climbFrom_, climbTo_, over);
            pos_.y = climbFrom_.y + (climbTopY_ - climbFrom_.y) * up;
        } else {
            pos_ = Lerp(climbFrom_, climbTo_, Smooth((c - 0.1f) / 0.6f));
            pos_.y = climbTopY_ + (climbTo_.y - climbTopY_) * Smooth((c - 0.3f) / 0.6f);
        }
        vel_ = {};
        if (climbT_ >= 1.f) climbing_ = false;
    } else {
        const float stanceMax = trans_ < 1.f ? 0.f : StanceSpeed(stance_);
        Vec3 want = Flat(in.moveDir);
        const float wl = want.Length();
        if (wl > 1.f) want = want * (1.f / wl);
        want = want * stanceMax;

        // accelerate like a body, not a cursor
        Vec3 dv = want - vel_;
        const float maxDv = accel * dt;
        if (dv.Length() > maxDv) dv = dv.Normalized() * maxDv;
        vel_ += dv;

        const float speed = vel_.Length();
        if (speed > 1e-4f) {
            const Vec3 dir = vel_ * (1.f / speed);
            // walls and heavy things stop her (low steps she just steps onto); light things get pushed
            const bool low = stance_ == Stance::Crawl || stance_ == Stance::Prone;
            const float heights[4] = {0.4f, 0.8f, 1.2f, 1.55f};
            const float lowHeights[4] = {0.12f, 0.3f, 0.45f, 0.6f};
            for (int k = 0; k < 4; ++k) {
                const float h = low ? lowHeights[k] : heights[k];
                RayHit hit;
                const float reach = speed * dt + (low ? 0.5f : bodyRadius);
                if (world.RayCast(pos_ + Vec3{0, h, 0}, dir, reach, hit, true)) {
                    const float m = world.BodyMass(hit.body);
                    if (m <= 0.f || m > 25.f) {
                        const float into = vel_.Dot(dir);
                        if (into > 0.f) vel_ -= dir * into;
                        fb_.bumped = true;
                        break;
                    }
                }
            }
            // don't walk off a drop: step down at most a stair's height
            const Vec3 ahead = pos_ + dir * 0.3f;
            const float g = GroundAt(world, Vec3{ahead.x, pos_.y, ahead.z});
            if (pos_.y - g > 0.38f) {
                const float into = vel_.Dot(dir);
                if (into > 0.f) vel_ -= dir * into;
                fb_.atEdge = true;
            }
        }
        pos_ += vel_ * dt;

        // stand on whatever is under her (floor, a step up to ~35 cm)
        const float g = GroundAt(world, pos_);
        if (g - pos_.y < 0.38f) pos_.y += (g - pos_.y) * std::clamp(dt * 10.f, 0.f, 1.f);

        // turn: toward the face target, else toward where she goes
        float targetYaw = yaw_;
        if (in.hasFace) targetYaw = in.faceYaw;
        else if (Flat(vel_).Length() > 0.1f) targetYaw = std::atan2(vel_.x, vel_.z);
        const float rate = Lying() ? turnRate * 0.25f : stance_ == Stance::Crawl ? turnRate * 0.5f : turnRate;
        const float canTurn = (stance_ == Stance::Sit || stance_ == Stance::Kneel || stance_ == Stance::LieBack) ? 0.f : 1.f;
        const float d = WrapAngle(targetYaw - yaw_);
        yaw_ = WrapAngle(yaw_ + std::clamp(d, -rate * dt, rate * dt) * canTurn);
    }

    ch.placement.t = pos_ + Vec3{0, ch.floorOffset, 0};
    ch.placement.r = Quat::AxisAngle({0, 1, 0}, yaw_);
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
        Vec3 target = h.reach && !climbing_ ? h.target : restHand_[i];
        if (r.upperarm[i] >= 0 && r.lowerarm[i] >= 0 && r.hand[i] >= 0) {
            const Vec3 sh = ch.BonePos(r.upperarm[i]);
            // arm + palm: the physical hand is the palm, ~8 cm past the wrist bone
            const float len = (Distance(sh, ch.BonePos(r.lowerarm[i])) + Distance(ch.BonePos(r.lowerarm[i]), ch.BonePos(r.hand[i]))) * 0.98f + kPalm;
            const Vec3 d = target - sh;
            deficit_[i] = h.reach ? d.Length() - len : 0.f;
            if (d.Length() > len) target = sh + d.Normalized() * len;
        }
        // relaxed hands follow the body gently; planted hands (crawling, climbing) bear weight
        const bool planted = stance_ == Stance::Crawl || stance_ == Stance::Prone || stance_ == Stance::Sit || climbing_;
        const float strength = h.reach ? std::max(10.f, armStrength * std::clamp(h.effort, 0.f, 1.f)) : planted ? 60.f : 25.f;
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

Motor::Targets Motor::Blend(const Targets& a, const Targets& b, float w) {
    Targets o;
    o.pelvisOffset = Lerp(a.pelvisOffset, b.pelvisOffset, w);
    o.tilt = Nlerp(a.tilt, b.tilt, w);
    o.lean = a.lean + (b.lean - a.lean) * w;
    o.headPitch = a.headPitch + (b.headPitch - a.headPitch) * w;
    for (int i = 0; i < 2; ++i) {
        o.foot[i] = Lerp(a.foot[i], b.foot[i], w);
        o.kneePole[i] = Lerp(a.kneePole[i], b.kneePole[i], w);
        o.hand[i] = Lerp(a.hand[i], b.hand[i], w);
        o.elbowPole[i] = Lerp(a.elbowPole[i], b.elbowPole[i], w);
        o.palmDown[i] = w < 0.5f ? a.palmDown[i] : b.palmDown[i];
        o.elbowOverride[i] = a.elbowOverride[i] || b.elbowOverride[i];
    }
    o.footFollow = w < 0.5f ? a.footFollow : b.footFollow;
    return o;
}

void Motor::GaitFeet(Character& ch, World& world, float dt, float stride, float lift, Vec3 out[2]) {
    const RigMap& r = ch.Rig();
    const float speed = fb_.speed;
    const bool moving = speed > 0.05f;
    const float freq = 0.85f + 0.55f * speed;
    if (moving || footSwing_[0] || footSwing_[1]) gaitPhase_ += freq * dt;
    const Vec3 lead = vel_ * (stride / freq);
    for (int i = 0; i < 2; ++i) {
        if (r.foot[i] < 0) continue;
        Vec3 rest = ch.BindPos(r.foot[i]);
        // feet stand on whatever is under each one (a step, a slope)
        Vec3 n;
        rest.y = GroundAt(world, rest, &n) + dims_.ankleH;
        const float ph = std::fmod(gaitPhase_ + 0.5f * i, 1.f);
        const bool wantSwing = moving ? ph < 0.4f : Distance(Flat(footPlant_[i]), Flat(rest)) > 0.07f && !footSwing_[1 - i];
        if (wantSwing && !footSwing_[i]) {
            footSwing_[i] = true;
            footFrom_[i] = footPlant_[i];
            if (!moving) gaitPhase_ = std::floor(gaitPhase_) + (i == 0 ? 0.f : 0.5f);
        }
        if (footSwing_[i]) {
            const float s = std::clamp((moving ? ph : std::fmod(gaitPhase_ + 0.5f * i, 1.f)) / 0.4f, 0.f, 1.f);
            Vec3 land = rest + (moving ? lead : Vec3{});
            Vec3 ln;
            land.y = GroundAt(world, land, &ln) + dims_.ankleH;
            Vec3 p = footFrom_[i] + (land - footFrom_[i]) * Smooth(s);
            // clear the higher of the two surfaces (stepping up onto something)
            const float rise = std::max(0.f, land.y - footFrom_[i].y);   // stepping up: lift the foot over the edge
            p.y = Lerp(footFrom_[i], land, Smooth(s)).y + (lift + rise * 0.6f) * std::sin(kPi * s);
            footPlant_[i] = p;
            footNormal_[i] = ln;
            if (s >= 0.98f || (moving && ph >= 0.4f)) {
                footSwing_[i] = false;
                footPlant_[i] = land;
            }
        } else {
            footNormal_[i] = n;
        }
        out[i] = footPlant_[i];
    }
}

Motor::Targets Motor::StanceTargets(Character& ch, World& world, Stance st, float dt) {
    const RigMap& r = ch.Rig();
    Targets t;
    const Vec3 F = ch.placement.r.Rotate({0, 0, 1});
    const Vec3 U{0, 1, 0};
    const Vec3 L = U.Cross(F).Normalized({1, 0, 0});
    const Vec3 P0 = ch.BindPos(r.pelvis);            // standing hip position here
    const float g = pos_.y;
    const float ankle = dims_.ankleH;
    auto side = [](int i) { return i == 0 ? 1.f : -1.f; };
    Vec3 footRest[2], kneeRest[2], shRest[2];
    for (int i = 0; i < 2; ++i) {
        footRest[i] = ch.BindPos(r.foot[i]);
        kneeRest[i] = ch.BindPos(r.calf[i]);
        shRest[i] = ch.BindPos(r.upperarm[i]);
    }
    const float speed = fb_.speed;
    const float sf = StanceSpeed(st) > 0.f ? speed / StanceSpeed(st) : 0.f;

    switch (st) {
    case Stance::Stand:
    case Stance::Crouch: {
        const bool crouch = st == Stance::Crouch;
        Vec3 feet[2];
        GaitFeet(ch, world, dt, crouch ? 0.22f : 0.32f, crouch ? 0.05f : 0.075f, feet);
        const float bob = std::cos(gaitPhase_ * 4.f * kPi);
        if (crouch) {
            t.pelvisOffset = U * (-dims_.hipH * 0.46f) - F * 0.13f;
            t.lean = 0.62f;
        } else if (speed > 0.05f) {
            t.pelvisOffset = Vec3{0, -0.025f - 0.012f * bob, 0} + L * (0.018f * std::sin(gaitPhase_ * 2.f * kPi));
        }
        // the hips can't sit higher than the lower foot allows (standing on a step)
        const float lowFoot = std::min(feet[0].y, feet[1].y) - ankle;
        if (lowFoot < g) t.pelvisOffset.y += lowFoot - g;
        for (int i = 0; i < 2; ++i) {
            t.foot[i] = feet[i] + (crouch ? L * (side(i) * 0.06f) : Vec3{});
            t.kneePole[i] = kneeRest[i] + F * 1.f + L * (side(i) * (crouch ? 0.35f : 0.f));
            const float swing = crouch ? 0.f : 0.16f * (speed / maxSpeed) * std::sin(gaitPhase_ * 2.f * kPi + (i == 0 ? 0.f : kPi));
            if (crouch) {
                // forearms resting near the knees
                t.hand[i] = footRest[i] + F * 0.32f + U * (dims_.hipH * 0.42f) + L * (side(i) * 0.03f);
                t.elbowPole[i] = shRest[i] + L * (side(i) * 0.4f) - U * 0.3f;
                t.elbowOverride[i] = true;
            } else {
                t.hand[i] = shRest[i] + U * (-dims_.arm * 0.97f) + L * (side(i) * 0.05f) + F * (0.03f + swing);
                t.elbowPole[i] = shRest[i] - U * 0.3f - F * 0.5f + L * (side(i) * 0.1f);
            }
        }
        break;
    }
    case Stance::Kneel: {
        // upright on both knees, shins flat behind, hands resting on the thighs
        const Vec3 hip = Vec3{P0.x, g + 0.06f + dims_.thigh * 0.96f, P0.z};
        t.pelvisOffset = hip - P0;
        t.footFollow = true;
        for (int i = 0; i < 2; ++i) {
            const Vec3 knee = Vec3{P0.x, g + 0.05f, P0.z} + L * (side(i) * 0.1f) + F * 0.06f;
            t.foot[i] = knee - F * (dims_.shin * 0.93f) + U * 0.02f;
            t.kneePole[i] = knee + F * 1.f;
            t.hand[i] = hip + F * 0.25f + L * (side(i) * 0.13f) - U * 0.08f;
            t.elbowPole[i] = hip + L * (side(i) * 0.5f) - F * 0.2f + U * 0.2f;
            t.elbowOverride[i] = true;
        }
        break;
    }
    case Stance::Sit: {
        // sitting on the floor, legs out in front, leaning back on the hands
        const Vec3 hip = Vec3{P0.x, g + 0.11f, P0.z};
        t.pelvisOffset = hip - P0;
        t.tilt = Quat::AxisAngle(L, -0.22f);
        t.footFollow = true;
        for (int i = 0; i < 2; ++i) {
            t.foot[i] = hip + F * ((dims_.thigh + dims_.shin) * 0.86f) + L * (side(i) * 0.12f);
            t.foot[i].y = g + 0.06f;
            t.kneePole[i] = hip + F * 0.4f + U * 1.f + L * (side(i) * 0.1f);
            t.hand[i] = hip - F * 0.2f + L * (side(i) * 0.24f);
            t.hand[i].y = g + 0.05f;
            t.palmDown[i] = true;
            t.elbowPole[i] = hip - F * 0.6f + L * (side(i) * 0.4f) + U * 0.3f;
            t.elbowOverride[i] = true;
        }
        break;
    }
    case Stance::Crawl: {
        // hands and knees: torso level, thighs vertical, hands under the shoulders, diagonal crawling gait
        if (speed > 0.03f) crawlPhase_ += dt * (0.8f + 1.6f * sf);
        const Vec3 hip = Vec3{P0.x, g + 0.06f + dims_.thigh * 0.94f, P0.z};
        t.pelvisOffset = hip - P0;
        t.tilt = Quat::AxisAngle(L, 1.42f);
        t.footFollow = true;
        t.headPitch = 0.7f;
        const Vec3 shoulderC = hip + F * (dims_.torso * 0.95f);
        for (int i = 0; i < 2; ++i) {
            // left hand moves with the right knee, and vice versa
            const float phHand = crawlPhase_ * 2.f * kPi + (i == 0 ? 0.f : kPi);
            const float phKnee = phHand + kPi;
            const float stride = 0.1f * sf;
            const Vec3 knee = Vec3{hip.x, g + 0.05f, hip.z} + L * (side(i) * 0.11f) + F * (stride * std::sin(phKnee)) +
                              U * (0.04f * sf * std::max(0.f, std::cos(phKnee)));
            t.foot[i] = knee - F * (dims_.shin * 0.92f) + U * 0.03f;
            t.kneePole[i] = knee + F * 0.6f - U * 0.3f;
            t.hand[i] = shoulderC + L * (side(i) * 0.16f) + F * (0.04f + stride * std::sin(phHand));
            t.hand[i].y = g + 0.06f + 0.05f * sf * std::max(0.f, std::cos(phHand));
            t.palmDown[i] = true;
            t.elbowPole[i] = shoulderC - F * 0.4f + L * (side(i) * 0.35f) + U * 0.2f;
            t.elbowOverride[i] = true;
        }
        break;
    }
    case Stance::Prone: {
        // flat on the front, propped on the forearms; army crawl pulls with alternating elbows and knees
        if (speed > 0.02f) crawlPhase_ += dt * (0.6f + 1.2f * sf);
        const Vec3 hip = Vec3{P0.x, g + 0.11f, P0.z};
        t.pelvisOffset = hip - P0;
        const Quat tilt = Quat::AxisAngle(L, kPi * 0.5f - 0.04f);
        t.tilt = tilt;
        t.footFollow = true;
        t.headPitch = 0.9f;
        const Vec3 shoulderC = hip + F * (dims_.torso * 0.97f);
        for (int i = 0; i < 2; ++i) {
            const float ph = crawlPhase_ * 2.f * kPi + (i == 0 ? 0.f : kPi);
            // straight legs lie behind, one knee draws up to the side while crawling
            Vec3 foot = hip + tilt.Rotate(footRest[i] - P0);
            const float draw = 0.25f * sf * std::max(0.f, std::sin(ph + kPi));
            foot = foot + F * draw + L * (side(i) * draw * 0.6f);
            foot.y = g + 0.05f;
            t.foot[i] = foot;
            t.kneePole[i] = hip - U * 1.f + L * (side(i) * 0.6f);
            t.hand[i] = shoulderC + F * (0.22f + 0.1f * sf * std::sin(ph)) + L * (side(i) * 0.14f);
            t.hand[i].y = g + 0.05f;
            t.palmDown[i] = true;
            t.elbowPole[i] = shoulderC - U * 0.6f + L * (side(i) * 0.4f) - F * 0.1f;
            t.elbowOverride[i] = true;
        }
        break;
    }
    case Stance::LieBack: {
        // on her back, arms by her sides; this is how she sleeps
        RayHit bed;
        float surf = g;
        if (world.RayCast(Vec3{P0.x, P0.y + 0.3f, P0.z}, {0, -1, 0}, 1.6f, bed, true)) surf = std::max(g, bed.point.y);
        const Vec3 hip = Vec3{P0.x, surf + 0.1f, P0.z};
        t.pelvisOffset = hip - P0;
        const Quat tilt = Quat::AxisAngle(L, -(kPi * 0.5f - 0.04f));
        t.tilt = tilt;
        t.footFollow = true;
        for (int i = 0; i < 2; ++i) {
            Vec3 foot = hip + tilt.Rotate(footRest[i] - P0);
            foot.y = surf + 0.07f;
            t.foot[i] = foot;
            t.kneePole[i] = hip + U * 1.f + F * 0.3f;
            t.hand[i] = hip - F * 0.08f + L * (side(i) * 0.26f);
            t.hand[i].y = surf + 0.05f;
            t.elbowPole[i] = hip - F * 0.3f + L * (side(i) * 0.6f);
            t.elbowOverride[i] = true;
        }
        break;
    }
    default: break;
    }
    return t;
}

void Motor::PostPhysics(Character& ch, World& world, PostureParams base, float dt) {
    const RigMap& r = ch.Rig();
    const Vec3 F = ch.placement.r.Rotate({0, 0, 1});
    const Vec3 U{0, 1, 0};
    const Vec3 L = U.Cross(F).Normalized({1, 0, 0});

    Targets tg;
    if (climbing_) {
        // hands on the edge, hips go up and over, a knee/foot onto the top, then stand
        const float c = std::min(1.f, climbT_);
        Targets standT = StanceTargets(ch, world, Stance::Stand, dt);
        tg = standT;
        const float handsOn = Smooth(c / 0.2f) * (1.f - Smooth((c - 0.75f) / 0.2f));
        const float crouchy = std::sin(kPi * std::clamp((c - 0.1f) / 0.8f, 0.f, 1.f));
        tg.lean = 0.9f * crouchy;
        tg.pelvisOffset = U * (-0.25f * crouchy);
        const float edgeY = climbDown_ ? climbTopY_ : climbTopY_;
        for (int i = 0; i < 2; ++i) {
            const float s = i == 0 ? 1.f : -1.f;
            const Vec3 grab = Vec3{climbEdge_.x, edgeY + 0.05f, climbEdge_.z} + L * (s * 0.22f) + climbDir_ * (climbDown_ ? -0.05f : 0.08f);
            tg.hand[i] = Lerp(tg.hand[i], grab, handsOn);
            tg.elbowPole[i] = ch.BindPos(r.upperarm[i]) + L * (s * 0.5f) - U * 0.2f;
            tg.elbowOverride[i] = true;
            tg.kneePole[i] = ch.BindPos(r.calf[i]) + F * 1.f + L * (s * 0.2f);
            // lead leg (right) goes up onto the edge early
            if (!climbDown_ && i == 1) {
                const float k = Smooth((c - 0.25f) / 0.3f) * (1.f - Smooth((c - 0.85f) / 0.15f));
                const Vec3 onTop = Vec3{climbEdge_.x, climbTopY_ + dims_.ankleH, climbEdge_.z} + climbDir_ * 0.12f + L * (s * 0.1f);
                tg.foot[i] = Lerp(tg.foot[i], onTop, k);
            }
        }
        for (int i = 0; i < 2; ++i) footPlant_[i] = tg.foot[i];
    } else {
        Targets cur = StanceTargets(ch, world, stance_, dt);
        if (trans_ < 1.f) {
            Targets nxt = StanceTargets(ch, world, next_, dt);
            tg = Blend(cur, nxt, Smooth(trans_));
        } else {
            tg = cur;
        }
    }

    base.pelvisOffset = base.pelvisOffset + tg.pelvisOffset;
    base.bodyTilt = tg.tilt;
    base.footOverride = r.foot[0] >= 0 && r.foot[1] >= 0;
    base.footFollow = tg.footFollow;
    base.kneePoleOverride = true;
    base.headPitch = tg.headPitch;
    for (int i = 0; i < 2; ++i) {
        base.footTarget[i] = tg.foot[i];
        base.kneePole[i] = tg.kneePole[i];
        base.footNormal[i] = footNormal_[i];
        base.palmDown[i] = tg.palmDown[i] && !reaching_[i];
        base.elbowPoleOverride[i] = tg.elbowOverride[i] && !reaching_[i];
        base.elbowPole[i] = tg.elbowPole[i];
        // arms follow her physical hands when she uses them; otherwise the stance places them
        base.handOverride[i] = true;
        if ((reaching_[i] && !climbing_) || joint_[i] >= 0) {
            const Vec3 hp = world.BodyPosition(hand_[i]);
            const Vec3 el = r.lowerarm[i] >= 0 ? ch.BonePos(r.lowerarm[i]) : hp;
            base.handTarget[i] = hp - (hp - el).Normalized({0, -1, 0}) * kPalm;
        } else {
            base.handTarget[i] = tg.hand[i];
        }
    }

    // lean over / twist toward a reach that the arm alone can't make (a person bends at the waist)
    float wantLean = 0.f, wantTwist = 0.f;
    if (stance_ == Stance::Stand || stance_ == Stance::Crouch) {
        for (int i = 0; i < 2; ++i) {
            if (!reaching_[i]) continue;
            wantLean = std::max(wantLean, lean_ + deficit_[i] * 4.f);
            const Vec3 d = Flat(world.BodyPosition(hand_[i]) - pos_);
            if (d.Length() > 0.05f) wantTwist = std::clamp(std::atan2(F.Cross(d.Normalized()).y, F.Dot(d.Normalized())) * 0.4f, -0.5f, 0.5f);
        }
    }
    wantLean = std::clamp(wantLean, 0.f, 1.3f);
    lean_ += (wantLean - lean_) * std::clamp(dt * 5.f, 0.f, 1.f);
    twist_ += (wantTwist - twist_) * std::clamp(dt * 3.f, 0.f, 1.f);
    base.lean = std::max(lean_, tg.lean);
    base.leanTwist = twist_;

    ApplyPosture(ch, base);

    // her torso body follows the posed chest (lying down it lies flat too)
    {
        const Vec3 pel = ch.BonePos(r.pelvis);
        const int top = r.SpineTop();
        const Vec3 chest = top >= 0 ? ch.BonePos(top) : pel + U * 0.4f;
        const Vec3 axis = (chest - pel).Normalized(U);
        world.SetKinematicTarget(torso_, (pel + chest) * 0.5f, Quat::FromTo(U, axis) * ch.placement.r);
    }

    // where relaxed hands are this frame (next frame's muscle target): palm a little past the wrist
    for (int i = 0; i < 2; ++i) {
        if (r.hand[i] < 0 || reaching_[i]) continue;
        const Vec3 wrist = ch.BonePos(r.hand[i]);
        const Vec3 el = r.lowerarm[i] >= 0 ? ch.BonePos(r.lowerarm[i]) : wrist;
        restHand_[i] = wrist + (wrist - el).Normalized({0, -1, 0}) * kPalm;
    }
    const float effortStance = stance_ == Stance::Crawl ? 0.25f : stance_ == Stance::Prone ? 0.3f : stance_ == Stance::Crouch ? 0.12f : 0.f;
    const float sMax = std::max(0.1f, StanceSpeed(stance_));
    fb_.exertion = std::clamp(0.05f + effortStance + (climbing_ ? 0.45f : 0.f) + (trans_ < 1.f ? 0.1f : 0.f) +
                              0.25f * (fb_.speed / sMax) * (fb_.speed / sMax) +
                              0.4f * std::max(fb_.handForce[0], fb_.handForce[1]) / armStrength, 0.f, 1.f);
}

} // namespace eon
