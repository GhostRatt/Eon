#pragma once

#include <cstdint>
#include <memory>

#include "Math.h"

namespace eon {

// Thin wrapper over Jolt Physics. Units: meters, kilograms, seconds. Gravity -9.81 Y.
using BodyHandle = uint32_t;
constexpr BodyHandle kNoBody = 0xffffffffu;   // also means "fixed to world" in constraints
using ConstraintHandle = int;
constexpr ConstraintHandle kNoConstraint = -1;

enum class Shape : uint8_t { Box, Capsule };

struct BodyDesc {
    Shape shape = Shape::Box;
    Vec3 pos;
    Quat rot;
    Vec3 halfExtent{0.5f, 0.5f, 0.5f};  // box
    float radius = 0.01f;               // capsule
    float halfHeight = 0.02f;           // capsule cylinder half height (along local Y)
    float mass = 0.f;                   // 0 = static
    float friction = 0.6f;
    float restitution = 0.1f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool fastMoving = false;            // continuous collision for small/thin bodies
    bool kinematic = false;             // moved by MoveKinematic, pushes dynamic bodies
    bool ghost = false;                 // collides with nothing (hands/handles for constraints)
    bool agent = false;                 // Jenny's own body/hands: hits the world, never each other
    float gravityFactor = 1.f;
    uint32_t group = 0;                 // collision group (0 = none)
    uint32_t subGroup = 0;
    uint64_t userData = 0;
};

class Physics {
public:
    Physics();
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    void Clear();
    void Step(float dt);

    // Collision groups: bodies in the same group whose sub groups are marked don't collide.
    uint32_t NewGroup(uint32_t numSubGroups);
    void DisableCollision(uint32_t group, uint32_t subA, uint32_t subB);

    BodyHandle CreateBody(const BodyDesc& d);
    void RemoveBody(BodyHandle h);
    bool IsDynamic(BodyHandle h) const;

    void GetTransform(BodyHandle h, Vec3& pos, Quat& rot) const;
    Vec3 GetPosition(BodyHandle h) const;
    Vec3 GetLinearVelocity(BodyHandle h) const;
    Vec3 GetAngularVelocity(BodyHandle h) const;
    Vec3 GetVelocityAt(BodyHandle h, const Vec3& worldPoint) const;
    float GetMass(BodyHandle h) const;
    uint64_t GetUserData(BodyHandle h) const;

    void AddForce(BodyHandle h, const Vec3& force, const Vec3& worldPoint);
    void AddTorque(BodyHandle h, const Vec3& torque);
    void SetAngularVelocity(BodyHandle h, const Vec3& w);
    void Wake(BodyHandle h);
    void SetTransform(BodyHandle h, const Vec3& pos, const Quat& rot);  // teleport
    void MoveKinematic(BodyHandle h, const Vec3& pos, const Quat& rot, float dt);

    // Constraints (points/axes in world space at creation time). Pass kNoBody for "world".
    ConstraintHandle AddFixed(BodyHandle a, BodyHandle b);
    ConstraintHandle AddPoint(BodyHandle a, BodyHandle b, const Vec3& worldPoint);
    // Slider along axis; position 0 = creation pose. A spring motor pulls it back to 0 with a capped force.
    ConstraintHandle AddSpringSlider(BodyHandle a, BodyHandle b, const Vec3& point, const Vec3& axis,
                                     float minPos, float maxPos, float springHz, float damping, float maxForce);
    // Hinge; angle = signed angle from normalA (on a) to normalB (on b) around axis.
    ConstraintHandle AddHinge(BodyHandle a, BodyHandle b, const Vec3& point, const Vec3& axis,
                              const Vec3& normalA, const Vec3& normalB, float minAngle, float maxAngle, float frictionTorque);
    void RemoveConstraint(ConstraintHandle c);
    bool IsConstraintValid(ConstraintHandle c) const;

    float SliderPosition(ConstraintHandle c) const;
    float HingeAngle(ConstraintHandle c) const;
    // Magnitude of the force (N) the constraint applied during the last step (fixed/point only)
    float ConstraintForce(ConstraintHandle c, float dt) const;

    // Closest hit along a ray. Returns false on miss.
    bool RayCast(const Vec3& origin, const Vec3& dir, float maxDist, BodyHandle& hit, Vec3& point, float& dist,
                 BodyHandle ignore = kNoBody, bool skipAgent = false) const;
    Vec3 GetHitNormal(BodyHandle h, const Vec3& point) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eon
