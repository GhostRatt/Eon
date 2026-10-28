#include "Physics.h"

#include <algorithm>
#include <thread>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace eon {

namespace {

namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kGhost = 2;
constexpr JPH::ObjectLayer kAgent = 3;
}

class BPLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return JPH::BroadPhaseLayer(l == Layers::kStatic ? 0 : 1);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l.GetValue() == 0 ? "static" : "moving"; }
#endif
};

class ObjVsBP final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l, JPH::BroadPhaseLayer bp) const override {
        if (l == Layers::kGhost) return false;
        return l == Layers::kMoving || l == Layers::kAgent || bp.GetValue() == 1;
    }
};

class ObjPair final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::kGhost || b == Layers::kGhost) return false;
        if (a == Layers::kAgent && b == Layers::kAgent) return false;
        return a == Layers::kMoving || b == Layers::kMoving || a == Layers::kAgent || b == Layers::kAgent;
    }
};

void InitJoltOnce() {
    static bool done = false;
    if (done) return;
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    done = true;
}

JPH::Vec3 J(const Vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
JPH::Quat J(const Quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
Vec3 E(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Quat E(JPH::QuatArg q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }
JPH::BodyID Id(BodyHandle h) { return JPH::BodyID(h); }

} // namespace

struct Physics::Impl {
    BPLayers bpLayers;
    ObjVsBP objVsBp;
    ObjPair objPair;
    JPH::TempAllocatorImpl temp{16 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                  static_cast<int>(std::max(1u, std::thread::hardware_concurrency()) - 1)};
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> bodies;
    std::vector<JPH::Ref<JPH::Constraint>> constraints;
    std::vector<JPH::Ref<JPH::GroupFilterTable>> groups{nullptr}; // index 0 = no group

    Impl() {
        system.Init(4096, 0, 8192, 8192, bpLayers, objVsBp, objPair);
        JPH::PhysicsSettings s = system.GetPhysicsSettings();
        s.mNumVelocitySteps = 16;  // stiffer ropes and stacks
        s.mNumPositionSteps = 4;
        system.SetPhysicsSettings(s);
        system.SetGravity(JPH::Vec3(0, -9.81f, 0));
    }

    JPH::BodyInterface& BI() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& BI() const { return system.GetBodyInterface(); }

    JPH::Constraint* Get(ConstraintHandle c) const {
        return c >= 0 && c < static_cast<int>(constraints.size()) ? constraints[c].GetPtr() : nullptr;
    }
    ConstraintHandle Add(JPH::TwoBodyConstraint* c) {
        if (!c) return kNoConstraint;
        system.AddConstraint(c);
        constraints.emplace_back(c);
        return static_cast<ConstraintHandle>(constraints.size()) - 1;
    }
};

Physics::Physics() {
    InitJoltOnce();
    impl_ = std::make_unique<Impl>();
}

Physics::~Physics() { Clear(); }

void Physics::Clear() {
    for (auto& c : impl_->constraints) if (c) impl_->system.RemoveConstraint(c);
    impl_->constraints.clear();
    for (const JPH::BodyID& id : impl_->bodies) {
        impl_->BI().RemoveBody(id);
        impl_->BI().DestroyBody(id);
    }
    impl_->bodies.clear();
    impl_->groups.assign(1, nullptr);
}

void Physics::Step(float dt) {
    impl_->system.Update(dt, 1, &impl_->temp, &impl_->jobs);
}

uint32_t Physics::NewGroup(uint32_t numSubGroups) {
    impl_->groups.emplace_back(new JPH::GroupFilterTable(numSubGroups));
    return static_cast<uint32_t>(impl_->groups.size()) - 1;
}

void Physics::DisableCollision(uint32_t group, uint32_t subA, uint32_t subB) {
    if (group == 0 || group >= impl_->groups.size()) return;
    impl_->groups[group]->DisableCollision(subA, subB);
}

BodyHandle Physics::CreateBody(const BodyDesc& d) {
    JPH::ShapeRefC shape;
    if (d.shape == Shape::HeightField) {
        JPH::HeightFieldShapeSettings hs(d.samples, JPH::Vec3(0, 0, 0), JPH::Vec3(d.cellX, 1.f, d.cellZ), static_cast<JPH::uint32>(d.sampleCount));
        hs.mBlockSize = 4;
        auto r = hs.Create();
        if (r.HasError()) return kNoBody;
        shape = r.Get();
    } else if (d.shape == Shape::Capsule) {
        shape = new JPH::CapsuleShape(std::max(d.halfHeight, 0.0005f), std::max(d.radius, 0.0005f));
    } else {
        const JPH::Vec3 he(std::max(d.halfExtent.x, 0.001f), std::max(d.halfExtent.y, 0.001f), std::max(d.halfExtent.z, 0.001f));
        const float cr = std::min(JPH::cDefaultConvexRadius, 0.5f * he.ReduceMin());
        shape = new JPH::BoxShape(he, cr);
    }

    const bool dynamic = d.mass > 0.f && !d.kinematic;
    const JPH::EMotionType motion = d.kinematic ? JPH::EMotionType::Kinematic
                                  : dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
    const JPH::ObjectLayer layer = d.ghost ? Layers::kGhost : d.agent ? Layers::kAgent
                                 : (motion == JPH::EMotionType::Static ? Layers::kStatic : Layers::kMoving);
    JPH::BodyCreationSettings s(shape, JPH::RVec3(d.pos.x, d.pos.y, d.pos.z), J(d.rot), motion, layer);
    s.mFriction = d.friction;
    s.mRestitution = d.restitution;
    s.mLinearDamping = d.linearDamping;
    s.mAngularDamping = d.angularDamping;
    s.mUserData = d.userData;
    s.mGravityFactor = d.gravityFactor;
    if (dynamic) {
        s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        s.mMassPropertiesOverride.mMass = d.mass;
        if (d.fastMoving) s.mMotionQuality = JPH::EMotionQuality::LinearCast;
    }
    if (d.group > 0 && d.group < impl_->groups.size()) {
        s.mCollisionGroup = JPH::CollisionGroup(impl_->groups[d.group], d.group, d.subGroup);
    }

    const JPH::BodyID id = impl_->BI().CreateAndAddBody(s, motion != JPH::EMotionType::Static ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (id.IsInvalid()) return kNoBody;
    impl_->bodies.push_back(id);
    return id.GetIndexAndSequenceNumber();
}

void Physics::RemoveBody(BodyHandle h) {
    auto& v = impl_->bodies;
    auto it = std::find(v.begin(), v.end(), Id(h));
    if (it == v.end()) return;
    impl_->BI().RemoveBody(*it);
    impl_->BI().DestroyBody(*it);
    v.erase(it);
}

bool Physics::IsDynamic(BodyHandle h) const {
    return h != kNoBody && impl_->BI().GetMotionType(Id(h)) == JPH::EMotionType::Dynamic;
}

void Physics::GetTransform(BodyHandle h, Vec3& pos, Quat& rot) const {
    JPH::RVec3 p;
    JPH::Quat q;
    impl_->BI().GetPositionAndRotation(Id(h), p, q);
    pos = E(JPH::Vec3(p));
    rot = E(q);
}

Vec3 Physics::GetPosition(BodyHandle h) const { return E(JPH::Vec3(impl_->BI().GetPosition(Id(h)))); }
Vec3 Physics::GetLinearVelocity(BodyHandle h) const { return E(impl_->BI().GetLinearVelocity(Id(h))); }
Vec3 Physics::GetAngularVelocity(BodyHandle h) const { return E(impl_->BI().GetAngularVelocity(Id(h))); }
Vec3 Physics::GetVelocityAt(BodyHandle h, const Vec3& p) const {
    return E(impl_->BI().GetPointVelocity(Id(h), JPH::RVec3(p.x, p.y, p.z)));
}

float Physics::GetMass(BodyHandle h) const {
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), Id(h));
    if (!lock.Succeeded()) return 0.f;
    const JPH::Body& b = lock.GetBody();
    if (!b.IsDynamic()) return 0.f;
    const float inv = b.GetMotionProperties()->GetInverseMass();
    return inv > 0.f ? 1.f / inv : 0.f;
}

uint64_t Physics::GetUserData(BodyHandle h) const { return impl_->BI().GetUserData(Id(h)); }

void Physics::AddForce(BodyHandle h, const Vec3& f, const Vec3& p) {
    if (!IsDynamic(h)) return;
    impl_->BI().AddForce(Id(h), J(f), JPH::RVec3(p.x, p.y, p.z));
}

void Physics::SetLinearVelocity(BodyHandle h, const Vec3& v) {
    if (!IsDynamic(h)) return;
    impl_->BI().SetLinearVelocity(Id(h), J(v));
}

void Physics::AddTorque(BodyHandle h, const Vec3& t) {
    if (!IsDynamic(h)) return;
    impl_->BI().AddTorque(Id(h), J(t));
}

void Physics::SetAngularVelocity(BodyHandle h, const Vec3& w) {
    if (IsDynamic(h)) impl_->BI().SetAngularVelocity(Id(h), J(w));
}

void Physics::SetTransform(BodyHandle h, const Vec3& p, const Quat& q) {
    impl_->BI().SetPositionAndRotation(Id(h), JPH::RVec3(p.x, p.y, p.z), J(q),
                                       IsDynamic(h) ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void Physics::MoveKinematic(BodyHandle h, const Vec3& p, const Quat& q, float dt) {
    impl_->BI().MoveKinematic(Id(h), JPH::RVec3(p.x, p.y, p.z), J(q), dt);
}

void Physics::Wake(BodyHandle h) {
    if (IsDynamic(h)) impl_->BI().ActivateBody(Id(h));
}

ConstraintHandle Physics::AddFixed(BodyHandle a, BodyHandle b) {
    JPH::FixedConstraintSettings s;
    s.mSpace = JPH::EConstraintSpace::WorldSpace;
    s.mAutoDetectPoint = true;
    return impl_->Add(impl_->BI().CreateConstraint(&s, Id(a), Id(b)));
}

ConstraintHandle Physics::AddPoint(BodyHandle a, BodyHandle b, const Vec3& p) {
    JPH::PointConstraintSettings s;
    s.mSpace = JPH::EConstraintSpace::WorldSpace;
    s.mPoint1 = s.mPoint2 = JPH::RVec3(p.x, p.y, p.z);
    return impl_->Add(impl_->BI().CreateConstraint(&s, Id(a), Id(b)));
}

ConstraintHandle Physics::AddSpringSlider(BodyHandle a, BodyHandle b, const Vec3& p, const Vec3& axis,
                                          float minPos, float maxPos, float hz, float damping, float maxForce) {
    JPH::SliderConstraintSettings s;
    s.mSpace = JPH::EConstraintSpace::WorldSpace;
    s.mPoint1 = s.mPoint2 = JPH::RVec3(p.x, p.y, p.z);
    s.SetSliderAxis(J(axis.Normalized()));
    s.mLimitsMin = minPos;
    s.mLimitsMax = maxPos;
    s.mMotorSettings = JPH::MotorSettings(hz, damping);
    s.mMotorSettings.SetForceLimit(maxForce);
    auto* c = static_cast<JPH::SliderConstraint*>(impl_->BI().CreateConstraint(&s, Id(a), Id(b)));
    if (c) {
        c->SetMotorState(JPH::EMotorState::Position);
        c->SetTargetPosition(0.f);
    }
    return impl_->Add(c);
}

ConstraintHandle Physics::AddHinge(BodyHandle a, BodyHandle b, const Vec3& p, const Vec3& axis,
                                   const Vec3& normalA, const Vec3& normalB, float minAngle, float maxAngle, float friction) {
    JPH::HingeConstraintSettings s;
    s.mSpace = JPH::EConstraintSpace::WorldSpace;
    s.mPoint1 = s.mPoint2 = JPH::RVec3(p.x, p.y, p.z);
    s.mHingeAxis1 = s.mHingeAxis2 = J(axis.Normalized());
    s.mNormalAxis1 = J(normalA.Normalized());
    s.mNormalAxis2 = J(normalB.Normalized());
    s.mLimitsMin = minAngle;
    s.mLimitsMax = maxAngle;
    s.mMaxFrictionTorque = friction;
    return impl_->Add(impl_->BI().CreateConstraint(&s, Id(a), Id(b)));
}

void Physics::RemoveConstraint(ConstraintHandle c) {
    if (JPH::Constraint* k = impl_->Get(c)) {
        impl_->system.RemoveConstraint(k);
        impl_->constraints[c] = nullptr;
    }
}

bool Physics::IsConstraintValid(ConstraintHandle c) const { return impl_->Get(c) != nullptr; }

float Physics::SliderPosition(ConstraintHandle c) const {
    auto* k = impl_->Get(c);
    return k && k->GetSubType() == JPH::EConstraintSubType::Slider ? static_cast<JPH::SliderConstraint*>(k)->GetCurrentPosition() : 0.f;
}

float Physics::HingeAngle(ConstraintHandle c) const {
    auto* k = impl_->Get(c);
    return k && k->GetSubType() == JPH::EConstraintSubType::Hinge ? static_cast<JPH::HingeConstraint*>(k)->GetCurrentAngle() : 0.f;
}

void Physics::SetHingeLimits(ConstraintHandle c, float minAngle, float maxAngle) {
    auto* k = impl_->Get(c);
    if (k && k->GetSubType() == JPH::EConstraintSubType::Hinge) static_cast<JPH::HingeConstraint*>(k)->SetLimits(minAngle, maxAngle);
}

float Physics::ConstraintForce(ConstraintHandle c, float dt) const {
    auto* k = impl_->Get(c);
    if (!k || dt <= 0.f) return 0.f;
    if (k->GetSubType() == JPH::EConstraintSubType::Fixed)
        return static_cast<JPH::FixedConstraint*>(k)->GetTotalLambdaPosition().Length() / dt;
    if (k->GetSubType() == JPH::EConstraintSubType::Point)
        return static_cast<JPH::PointConstraint*>(k)->GetTotalLambdaPosition().Length() / dt;
    return 0.f;
}

namespace {
class SkipAgentFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l) const override { return l != Layers::kAgent && l != Layers::kGhost; }
};
}

Vec3 Physics::GetHitNormal(BodyHandle h, const Vec3& p) const {
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), Id(h));
    if (!lock.Succeeded()) return {0, 1, 0};
    const JPH::Body& b = lock.GetBody();
    const JPH::Vec3 n = b.GetWorldSpaceSurfaceNormal(JPH::SubShapeID(), JPH::RVec3(p.x, p.y, p.z));
    return E(n);
}

bool Physics::RayCast(const Vec3& origin, const Vec3& dir, float maxDist, BodyHandle& hit, Vec3& point, float& dist,
                      BodyHandle ignore, bool skipAgent) const {
    const Vec3 d = dir.Normalized() * maxDist;
    JPH::RRayCast ray{JPH::RVec3(origin.x, origin.y, origin.z), J(d)};
    JPH::RayCastResult result;
    JPH::IgnoreSingleBodyFilter filter(ignore == kNoBody ? JPH::BodyID() : Id(ignore));
    const JPH::BodyFilter defaultFilter;
    const JPH::BodyFilter& bodyFilter = ignore == kNoBody ? defaultFilter : static_cast<const JPH::BodyFilter&>(filter);
    const SkipAgentFilter skip;
    const JPH::ObjectLayerFilter all;
    const bool ok = impl_->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, skipAgent ? static_cast<const JPH::ObjectLayerFilter&>(skip) : all, bodyFilter);
    if (!ok) return false;
    hit = result.mBodyID.GetIndexAndSequenceNumber();
    dist = result.mFraction * maxDist;
    point = origin + dir.Normalized() * dist;
    return true;
}

} // namespace eon
