#pragma once

#include <array>

#include "Character.h"
#include "Posture.h"
#include "core/World.h"

namespace eon {

// Motor primitives: the ONLY way Jenny acts on the world. Everything is expressed as body movement
// (where to step, where to put a hand, whether to close the fingers). There is no "use object" command.
enum class HandShape : int { Auto, Open, Point, Fist };

struct HandIntent {
    bool reach = false;        // move the hand toward `target`
    Vec3 target;               // world point, or (local = true) relative to her feet/facing: x left, y up, z forward
    bool local = false;        // target moves with her body (carrying something while walking)
    HandShape shape = HandShape::Auto;   // Point: press with the index fingertip
    bool grip = false;         // close the hand: grabs whatever the palm is touching
    float effort = 0.5f;       // 0..1 of max arm strength (pressing harder, lifting heavier)
};

enum class Stance : int { Stand, Crouch, Kneel, Sit, Crawl, Prone, LieBack, LieLeft, LieRight, SitSeat, Count };
const char* StanceName(Stance s);

struct MotorIntent {
    Vec3 moveDir;              // world XZ direction, length = speed fraction 0..1 (1 = brisk walk)
    bool hasFace = false;      // turn the body to face this yaw (radians, 0 = +Z)
    float faceYaw = 0.f;
    bool hasLook = false;      // head/eyes look here
    Vec3 lookAt;
    std::array<HandIntent, 2> hands;   // 0 left, 1 right
    Stance stance = Stance::Stand;     // body position to get into (she moves through the in-between ones)
    bool climb = false;                // with moveDir: climb up onto / down off what's in front of her
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
    bool atEdge = false;             // a drop she won't just walk off
    bool climbing = false;
    Stance stance = Stance::Stand;   // current (or the one she is moving out of)
    bool inTransition = false;
    bool noSeat = false;             // tried to sit down but there is nothing behind her to sit on
    float load = 0.f;                // kg she is carrying (slows her down)
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
    int HeldBody(int i) const { return held_[i]; }
    int TipBody(int i) const { return tip_[i]; }
    float Lean() const { return lean_; }
    Stance CurrentStance() const { return stance_; }
    bool Lying() const { return stance_ == Stance::Prone || stance_ == Stance::LieBack || stance_ == Stance::LieLeft || stance_ == Stance::LieRight; }
    float Deficit(int i) const { return deficit_[i]; }
    float SupportY() const { return supportY_; }   // surface under her (floor, or the bed she lies on)

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
    void UpdateStance(Character& ch, World& world, const MotorIntent& in, float dt);
    bool BedRoom(World& world, const Vec3& dir) const;
    void StartClimb(Character& ch, World& world, const MotorIntent& in);

    struct Dims { float thigh = 0.42f, shin = 0.42f, hipH = 0.95f, ankleH = 0.09f, torso = 0.45f, arm = 0.5f; };
    struct Targets {
        Vec3 pelvisOffset;
        Quat tilt;
        float lean = 0.f, headPitch = 0.f, headRoll = 0.f;
        Vec3 headRollAxis{0, 0, 1};
        float floorY = 0.f;
        Vec3 foot[2], kneePole[2], hand[2], elbowPole[2], fingers[2];
        bool footFollow = false, palmDown[2] = {false, false}, elbowOverride[2] = {false, false};
    };
    Targets StanceTargets(Character& ch, World& world, Stance s, float dt);
    // the stance as it will be after this hop moves/turns her (rolling over, lying down along a bed, sitting up)
    Targets StanceTargetsAt(Character& ch, World& world, Stance s, float dt, float yaw, const Vec3& pos);
    float LieSurface(World& world, const Vec3& p) const;
    bool FindSeat(Character& ch, World& world);
    void GaitFeet(Character& ch, World& world, float dt, float stride, float lift, Vec3 out[2]);
    static Targets Blend(const Targets& a, const Targets& b, float w);
    float GroundAt(World& world, const Vec3& p, Vec3* normal = nullptr) const;
    // a ray that passes through whatever she is holding (her feet don't step onto the box in her arms)
    bool Ray(const World& world, const Vec3& from, const Vec3& dir, float len, RayHit& out) const;
    float StanceSpeed(Stance s) const;
    Dims dims_;

    int generation_ = -1;
    int torso_ = -1;
    std::array<int, 2> hand_{-1, -1};
    std::array<int, 2> tip_{-1, -1};          // index fingertip: a small body that presses small things when pointing
    std::array<float, 2> withdraw_{};
    std::array<Vec3, 2> withdrawTo_{};
    std::array<Quat, 2> holdRel_{};           // held thing's orientation relative to her facing when grabbed
    std::array<int, 2> joint_{-1, -1};
    std::array<int, 2> held_{-1, -1};
    std::array<Vec3, 2> restHand_{};
    std::array<int, 2> slip_{};
    std::array<bool, 2> reaching_{};
    std::array<bool, 2> gripWant_{};
    std::array<Vec3, 2> reachTarget_{};
    std::array<HandShape, 2> shape_{};
    Vec3 LocalToWorld(const Vec3& local) const;
    std::array<float, 2> curl_{0.25f, 0.25f}, spread_{}, thumb_{0.2f, 0.2f};
    float lean_ = 0.f, twist_ = 0.f;
    std::array<float, 2> deficit_{};   // how much further the hand needs to go than the arm reaches (m)

    Vec3 pos_;            // feet-center on the floor
    Vec3 vel_;
    float yaw_ = 0.f;
    float gaitPhase_ = 0.f;
    std::array<Vec3, 2> footPlant_{};
    std::array<Vec3, 2> footFrom_{};
    std::array<bool, 2> footSwing_{};
    std::array<Vec3, 2> footNormal_{Vec3{0, 1, 0}, Vec3{0, 1, 0}};
    bool init_ = false;
    MotorFeedback fb_;

    Stance stance_ = Stance::Stand;      // where she is (or coming from)
    Stance next_ = Stance::Stand;        // where she is going this hop
    float trans_ = 1.f, transDur_ = 0.7f;
    float crawlPhase_ = 0.f;
    float supportY_ = 0.f;
    Vec3 seat_;                          // where her hips go on the chair/bed edge (top surface point)
    bool nextFrame_ = false;             // the stance being moved into has her facing/standing elsewhere
    float nextYaw_ = 0.f;
    Vec3 nextPos_;
    float plantTilt_ = 0.f;              // extra torso pitch so planted hands actually reach the floor (crawl/sit)

    // climbing
    bool climbing_ = false, climbDown_ = false;
    float climbT_ = 0.f, climbDur_ = 2.f;
    Vec3 climbFrom_, climbEdge_, climbTo_, climbDir_;
    float climbTopY_ = 0.f;
};

} // namespace eon
