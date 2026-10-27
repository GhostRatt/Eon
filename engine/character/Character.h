#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "CharacterAsset.h"

namespace eon {

// Key bones found by name on any common rig (UE Mannequin, Auto-Rig Pro, Mixamo, Blender .L/.R).
struct RigMap {
    int root = -1, pelvis = -1, spine[5] = {-1, -1, -1, -1, -1}, neck = -1, head = -1;
    int clavicle[2] = {-1, -1}, upperarm[2] = {-1, -1}, lowerarm[2] = {-1, -1}, hand[2] = {-1, -1};
    int thigh[2] = {-1, -1}, calf[2] = {-1, -1}, foot[2] = {-1, -1}, ball[2] = {-1, -1};
    int breast[2] = {-1, -1}, glute[2] = {-1, -1};
    int finger[2][5][3];   // [side][thumb,index,middle,ring,pinky][joint 1..3], -1 if missing

    RigMap() { for (auto& s : finger) for (auto& f : s) for (int& j : f) j = -1; }
    void Build(const CharacterAsset& a);
    int SpineTop() const;
};

// Body collision capsule fitted to the skin around a bone (model/world space at use time).
struct BodyCapsule {
    int bone = -1;
    Vec3 localA, localB;  // segment ends in the bone's local space
    float radius = 0.05f;
    int tag = 0;          // 0 torso/pelvis/neck/head, 1 thigh, 2 breast, 3 glute
    int side = -1;        // 0 left, 1 right, -1 center
};

struct SoftBone {
    int bone = -1;
    int tag = 2;                // 2 breast, 3 glute
    Vec3 localDir{0, 0, 1};     // bone-local direction from the bone into the tissue
    float frequency = 2.8f;     // Hz
    float damping = 0.28f;      // ratio
    float sag = 0.008f;         // m
    Vec3 maxOffset{0.025f, 0.035f, 0.02f};   // m: sideways, vertical, along the tissue
    float length = 0.08f;       // m
    float transScale = 0.4f, rotScale = 0.7f;
    float support = 0.f;        // 0 free .. 1 firmly supported (bra etc.)
    // runtime
    Vec3 pos, vel, lastRest;
    bool valid = false;
    // contact: skin points (bone frame) used to flatten the tissue against a floor/bed it is pressed into
    std::vector<Vec3> samples;
    float squash = 1.f;         // 1 = free, <1 = flattened along the contact direction
    float pressure = 0.f;       // 0..1 how hard it is pressed (felt)
};

class Character {
public:
    bool Init(const CharacterAsset* asset);
    const CharacterAsset* Asset() const { return asset_; }
    const RigMap& Rig() const { return rig_; }

    Transform placement;              // model -> world (position on the floor, facing)
    float floorOffset = 0.f;          // placement.t.y that puts her soles on y = 0
    float modelYaw = 0.f;             // auto-detected: rotates the model so it faces +Z

    // ---- Pose ----
    void ResetPose();                                  // bind pose
    std::vector<Transform>& Local() { return local_; }
    const std::vector<Transform>& World() const { return world_; }
    void ComputeWorld();                               // local -> world (includes placement)
    Vec3 BonePos(int bone) const { return bone >= 0 ? world_[bone].t : Vec3{}; }
    Vec3 BindPos(int bone) const;      // where the bone would be in the bind pose at the current placement

    // Rotates a bone (world-space delta) and refreshes world transforms of its subtree.
    void RotateBoneWorld(int bone, const Quat& delta);

    // Two-bone IK: places `end` at target, bending `mid` toward pole.
    // keepEndWorld: true keeps the end bone's world rotation (feet flat), false lets it follow the limb (relaxed hands).
    void SolveTwoBone(int a, int b, int c, const Vec3& target, const Vec3& pole, float weight = 1.f, bool keepEndWorld = true);

    // ---- Hands ----
    // Palm direction (out of the palm) and finger direction of a hand in world space.
    Vec3 PalmNormal(int side) const;
    Vec3 FingerDir(int side) const;
    // Turns the hand so the palm faces `palm` with fingers toward `fingers` (weight 0..1, wrist limited to maxRad).
    void OrientHand(int side, const Vec3& palm, const Vec3& fingers, float weight = 1.f, float maxRad = 1.9f);
    // curl 0 flat .. 1 fist; spread 0..1 fingers apart; thumb 0 out .. 1 across the palm.
    void PoseFingers(int side, float curl, float spread, float thumb, float indexCurl = -1.f);   // indexCurl >= 0: pointing
    bool HasFingers(int side) const { return hands_[side].valid; }

    // Body openings in world space (follow the head/pelvis): where she drinks/bites, and where she pees from.
    Vec3 MouthPos() const;
    Vec3 GroinPos() const;

    // Skin points of each foot (heel, sole, toes, instep) in their bone's frame, for floor contact.
    float LowestFootPoint(int side) const;
    void BuildContactSamples();

    // ---- Physical layers (run after the animated pose) ----
    void UpdateLimbCollision(float dt);                // hands/forearms/elbows stay on the skin
    void UpdateSoftTissue(float dt);                   // breast/glute spring-mass follow-through
    void ResetDynamics();

    std::vector<BodyCapsule>& Capsules() { return capsules_; }
    std::vector<SoftBone>& SoftBones() { return soft_; }
    void SetSupport(int tag, float s);                 // 2 breast, 3 glute
    float contactFloorY = -1e9f;                       // surface her body rests on (floor/bed): soft tissue flattens on it

    // ---- Morphs ----
    void SetMorph(const std::string& name, float weight);
    float GetMorph(const std::string& name) const;
    std::vector<std::string> MorphNames() const;

    // ---- Skinning (CPU, multithreaded). Output in world space. ----
    void Skin();
    const std::vector<Vec3>& SkinnedPos(size_t mesh) const { return outPos_[mesh]; }
    const std::vector<Vec3>& SkinnedNrm(size_t mesh) const { return outNrm_[mesh]; }

    float Height() const { return height_; }

    // Re-measures how far her soles are above the floor with the current morphs (e.g. FlatFeets when barefoot)
    // and updates floorOffset so she stands on y = 0. Returns the change (m).
    float RefitFloor();

private:
    void FitCapsules();
    void SetupSoftBones();
    void SetupHands();
    void ApplyTissueContact(float dt);
    struct HandFrame {
        bool valid = false;
        Vec3 palmLocal, fingerLocal;           // in the hand bone's frame
        Vec3 curlAxis[5][3];                   // per finger joint, in that joint's frame
        Vec3 spreadAxis[5];                    // at the first joint, in its frame
    };
    HandFrame hands_[2];
    Vec3 mouthLocal_, groinLocal_;   // in the head / pelvis bone frames
    struct Sample { int bone; Vec3 local; };
    std::vector<Sample> footSamples_[2];
    void RecomputeSubtree(int bone);
    Vec3 CapsuleA(const BodyCapsule& c) const { return world_[c.bone].Apply(c.localA); }
    Vec3 CapsuleB(const BodyCapsule& c) const { return world_[c.bone].Apply(c.localB); }
public:
    void CapsuleWorld(const BodyCapsule& c, Vec3& a, Vec3& b) const { a = CapsuleA(c); b = CapsuleB(c); }
private:

    const CharacterAsset* asset_ = nullptr;
    RigMap rig_;
    std::vector<Transform> local_, world_;
    std::vector<std::vector<int>> children_;
    std::vector<BodyCapsule> capsules_;
    std::vector<SoftBone> soft_;
    std::unordered_map<std::string, float> morphWeights_;
    std::vector<std::vector<Vec3>> outPos_, outNrm_;
    std::vector<Vec3> limbDelta_;   // smoothed hand/elbow corrections
    float height_ = 1.7f;
};

} // namespace eon
