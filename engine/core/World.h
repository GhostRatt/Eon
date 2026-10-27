#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Circuit.h"
#include "Math.h"
#include "Physics.h"
#include "Zone.h"

namespace eon {

// What the renderer draws. Contains no names/types: safe to feed to Jenny's eye cameras.
struct RenderPiece {
    Vec3 pos;
    Quat rot;
    Vec3 size;          // full box extents (capsules are drawn as their bounding box)
    Rgb color;
    float emissive = 0.f;
    int entity = 0;     // operator/editor highlighting only; stripped before the mind bridge
};

struct RenderLight {
    Vec3 pos;
    Vec3 color;         // linear intensity
};

struct RenderFrame {
    std::vector<RenderPiece> pieces;
    std::vector<RenderLight> lights;
};

struct RayHit {
    int body = -1;      // runtime body index
    int entity = 0;
    Vec3 point;
    float dist = 0.f;
};

// Runs one zone: Jolt physics + electricity, coupled every fixed step.
class World {
public:
    static constexpr double kFixedDt = 1.0 / 120.0;

    World();
    ~World();

    bool Load(const std::string& path, std::string* error = nullptr);
    void SetZone(const Zone& zone);   // load from memory
    void Reset();                     // rebuild runtime from the zone layout
    void Tick(double realDt);         // real-time frame step (clamped against hitches)
    void Advance(double seconds);     // exact simulated time (tests/tools)
    void SetPhysicsEnabled(bool on) { physicsOn_ = on; }
    bool PhysicsEnabled() const { return physicsOn_; }

    // ---- Physical interaction (operator now, Jenny's hands later) ----
    bool RayCast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out, bool skipAgent = false) const;
    Vec3 SurfaceNormal(int body, const Vec3& point) const;
    float BodyMass(int body) const;   // 0 = static/immovable

    // ---- Agent bodies (Jenny's torso/hands live in the same physics as everything else) ----
    int AddAgentBody(const BodyDesc& d);
    // Each fixed step a force (capped at maxForce N) drives the body toward target, like a muscle.
    void SetDrive(int body, const Vec3& target, float maxForce, float frequency = 12.f, float damping = 1.f);
    void ClearDrive(int body);
    void SetKinematicTarget(int body, const Vec3& pos, const Quat& rot);
    // Rigid joint between two bodies at a point (a grip). Returns a handle for Detach/JointForce.
    int Attach(int bodyA, int bodyB, const Vec3& worldPoint);
    void Detach(int joint);
    float JointForce(int joint) const;   // N during the last step
    Vec3 BodyVelocity(int body) const;
    Quat BodyRotation(int body) const;
    void BeginGrab(int body, const Vec3& worldPoint);
    void SetGrabTarget(const Vec3& worldTarget);
    void EndGrab();
    int GrabbedBody() const { return grab_.body; }
    Vec3 GrabPoint() const;
    // Continuous push force at a point on a body, until cleared (or for `seconds` if > 0).
    void SetPush(int body, const Vec3& worldPoint, const Vec3& force, float seconds = -1.f);
    void ClearPush();

    // Debug actuation (editor/tests): physically presses a button or flips a switch.
    void DebugActuate(int entityId, bool on);

    // ---- State ----
    Zone& GetZone() { return zone_; }
    const Zone& GetZone() const { return zone_; }
    Circuit& GetCircuit() { return circuit_; }
    const Circuit& GetCircuit() const { return circuit_; }
    const Part* PartOf(int entityId) const;
    double SimTime() const { return simTime_; }
    int Generation() const { return generation_; }   // bumps on every Reset (agent bodies must be re-added)
    const std::string& Path() const { return path_; }

    int EntityOfBody(int body) const;
    BodyHandle HandleOf(int body) const;
    Vec3 BodyPosition(int body) const;
    int MainBodyOf(int entityId) const;
    Physics& GetPhysics() { return *physics_; }

    // Electrical contact state, for tests and the editor overlay.
    int ClipAttachedEntity(int wireEntityId, int end) const;   // entity the clip is latched to, or 0
    int ClipBodyOf(int wireEntityId, int end) const;
    std::vector<Vec3> TerminalPositions(int entityId) const;

    void BuildRenderFrame(RenderFrame& out) const;

private:
    enum class Role : uint8_t { Main, Plunger, Lever, RopeSegment, Clip, Agent };

    struct RtBody {
        BodyHandle h = kNoBody;
        int entity = 0;
        Role role = Role::Main;
        Vec3 size;           // full extents for drawing
        Quat drawOffset;     // capsule: box drawn along its Y
        Rgb color;
    };
    struct Terminal {
        int body = -1;       // -1: not physical (ideal)
        Vec3 local;
        int part = -1;
        int term = 0;
        int entity = 0;
        int clip = -1;       // index into clips_ if this terminal is a wire clip
    };
    struct Clip {
        int body = -1;
        int terminal = -1;   // its own terminal
        int attachedTo = -1; // terminal index it is latched onto
        ConstraintHandle joint = kNoConstraint;
        float cooldown = 0.f;
        float breakForce = 12.f;
    };
    struct ButtonRt {
        int entity = 0, base = -1, plunger = -1;
        ConstraintHandle slider = kNoConstraint;
        float travel = 0.008f, force = 2.5f;
        Vec3 axisLocal{0, 1, 0};
    };
    struct SwitchRt {
        int entity = 0, base = -1, lever = -1;
        ConstraintHandle hinge = kNoConstraint;
        Vec3 axisLocal{0, 1, 0}, hingeLocal{0, 0, 1}, longLocal{1, 0, 0};
        float limit = 0.3f, detent = 0.03f, halfLong = 0.02f, halfP = 0.006f;
    };
    struct IdealLink { int partA, termA, partB, termB; };
    struct PushRt { int body = -1; Vec3 local; Vec3 force; float timeLeft = -1.f; int tag = 0; };
    // A grab is a kinematic, non-colliding "hand" point pinned to the object: strong like a real grip,
    // speed-limited like a real arm, and it slips if the object resists harder than maxForce.
    struct GrabRt {
        int body = -1;
        Vec3 local, target, handPos;
        float maxForce = 60.f, maxSpeed = 4.f;
        BodyHandle hand = kNoBody;
        ConstraintHandle joint = kNoConstraint;
        int overloadSteps = 0;   // consecutive steps above maxForce
    };

    void BuildEntity(Entity& e);
    void BuildRope(Entity& wire, const std::vector<std::pair<int, int>>& endLinks);
    int AddBody(const BodyDesc& d, int entity, Role role, const Vec3& drawSize, Rgb color, Quat drawOffset = {});
    int AddTerminal(int body, const Vec3& local, int part, int term, int entity);
    int FindTerminal(int entity, int term) const;
    Vec3 TerminalWorld(const Terminal& t) const;
    Vec3 ToLocal(int body, const Vec3& world) const;
    Vec3 ToWorld(int body, const Vec3& local) const;
    void AttachClip(int clipIndex, int terminalIndex);
    void DetachClip(int clipIndex);

    void FixedStep(float dt);
    void ApplyForces(float dt);
    void UpdateClips(float dt);
    void UpdateSwitchStates();
    void UpdateConnections();

    Zone zone_;
    Circuit circuit_;
    std::unique_ptr<Physics> physics_;
    std::string path_;

    std::vector<RtBody> bodies_;
    std::vector<Terminal> terminals_;
    std::vector<Clip> clips_;
    std::vector<ButtonRt> buttons_;
    std::vector<SwitchRt> switches_;
    std::vector<IdealLink> idealLinks_;
    std::vector<PushRt> pushes_;
    GrabRt grab_;
    struct DriveRt { int body = -1; Vec3 target; float maxForce = 0.f, freq = 12.f, damping = 1.f; };
    struct KinRt { int body = -1; Vec3 pos; Quat rot; };
    std::vector<DriveRt> drives_;
    std::vector<KinRt> kinematic_;
    std::vector<ConstraintHandle> joints_;
    float lastDt_ = static_cast<float>(kFixedDt);
    int generation_ = 0;

    bool physicsOn_ = true;
    double accumulator_ = 0.0;
    double simTime_ = 0.0;
};

} // namespace eon
