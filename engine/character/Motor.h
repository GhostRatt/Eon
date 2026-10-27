#pragma once

#include <array>

#include "Character.h"
#include "Posture.h"
#include "core/World.h"

namespace eon {

// Motor primitives: the ONLY way Jenny acts on the world. Everything is expressed as body movement
// (where to step, where to put a hand, whether to close the fingers). There is no "use object" command.
struct HandIntent {
    bool reach = false;        // move the hand toward `target` (world point)
    Vec3 target;
    bool grip = false;         // close the hand: grabs whatever the palm is touching
    float effort = 0.5f;       // 0..1 of max arm strength (pressing harder, lifting heavier)
};

struct MotorIntent {
    Vec3 moveDir;              // world XZ direction, length = speed fraction 0..1 (1 = brisk walk)
    bool hasFace = false;      // turn the body to face this yaw (radians, 0 = +Z)
    float faceYaw = 0.f;
    bool hasLook = false;      // head/eyes look here
    Vec3 lookAt;
    std::array<HandIntent, 2> hands;   // 0 left, 1 right
};

// What her body physically feels from moving (fed to BodyState / the mind's proprioception).
struct MotorFeedback {
    float speed = 0.f;               // m/s
    float exertion = 0.f;            // 0..1
    std::array<bool, 2> holding{};   // something is in the hand
    std::array<float, 2> handForce{};// N the hand is pushing/pulling with
    std::array<bool, 2> handBlocked{};// hand can't get to where it's trying to go
    std::array<float, 2> heldMass{}; // kg of what's in the hand (felt as weight)
    bool bumped = false;             // walked into something solid this frame
};

class Motor {
public:
    void Init(Character& ch, World& world);
    // Before world.Tick: steer her physical bodies.
    void PrePhysics(Character& ch, World& world, const MotorIntent& in, float dt);
    // After world.Tick: build the pose (gait, arms following her hands) on top of the base posture.
    void PostPhysics(Character& ch, World& world, PostureParams base, float dt);

    const MotorFeedback& Feedback() const { return fb_; }
    float Yaw() const { return yaw_; }
    Vec3 Position() const { return pos_; }
    void Teleport(Character& ch, const Vec3& pos, float yaw);
    int HandBody(int i) const { return hand_[i]; }
    float Lean() const { return lean_; }
    float Deficit(int i) const { return deficit_[i]; }

    // Tuning (adult woman)
    float maxSpeed = 1.4f;         // m/s brisk walk
    float accel = 2.5f;            // m/s^2
    float turnRate = 3.f;          // rad/s
    float bodyRadius = 0.2f;
    float armStrength = 60.f;      // N max hand force
    float gripStrength = 120.f;    // N before the grip slips

private:
    void EnsureBodies(Character& ch, World& world);
    void StepLocomotion(Character& ch, World& world, const MotorIntent& in, float dt);
    void UpdateHands(Character& ch, World& world, const MotorIntent& in, float dt);

    int generation_ = -1;
    int torso_ = -1;
    std::array<int, 2> hand_{-1, -1};
    std::array<int, 2> joint_{-1, -1};
    std::array<int, 2> held_{-1, -1};
    std::array<Vec3, 2> restHand_{};
    std::array<int, 2> slip_{};
    std::array<bool, 2> reaching_{};
    float lean_ = 0.f, twist_ = 0.f;
    std::array<float, 2> deficit_{};   // how much further the hand needs to go than the arm reaches (m)

    Vec3 pos_;            // feet-center on the floor
    Vec3 vel_;
    float yaw_ = 0.f;
    float gaitPhase_ = 0.f;
    std::array<Vec3, 2> footPlant_{};
    std::array<Vec3, 2> footFrom_{};
    std::array<bool, 2> footSwing_{};
    bool init_ = false;
    MotorFeedback fb_;
};

} // namespace eon
