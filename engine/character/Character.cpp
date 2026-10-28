#include "Character.h"

#include "core/Jobs.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace eon {

namespace {

constexpr float kPi = 3.14159265358979f;

Quat Nlerp(const Quat& a, const Quat& b, float t) {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0 ? -1.f : 1.f;
    return Quat{a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t}.Normalized();
}

Vec3 ClosestOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float l2 = ab.Dot(ab);
    if (l2 < 1e-10f) return a;
    const float t = std::clamp((p - a).Dot(ab) / l2, 0.f, 1.f);
    return a + ab * t;
}

bool PushOut(Vec3& p, float r, const Vec3& a, const Vec3& b, float cr) {
    const Vec3 c = ClosestOnSegment(p, a, b);
    Vec3 d = p - c;
    const float minD = r + cr;
    const float len = d.Length();
    if (len >= minD) return false;
    d = len > 1e-6f ? d * (1.f / len) : Vec3{0, 0, 1};
    p = c + d * minD;
    return true;
}

Vec3 Lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

} // namespace

// ---------------- RigMap ----------------

void RigMap::Build(const CharacterAsset& a) {
    root = a.FindBoneAny({"root", "Armature", "c_traj"});
    pelvis = a.FindBoneAny({"pelvis", "hips", "hip", "root.x", "Bip01 Pelvis"});
    spine[0] = a.FindBoneAny({"spine_01", "spine", "spine_01.x", "spine1"});
    spine[1] = a.FindBoneAny({"spine_02", "spine1", "spine_02.x", "spine2", "chest"});
    spine[2] = a.FindBoneAny({"spine_03", "spine2", "spine_03.x", "spine3", "upperchest"});
    spine[3] = a.FindBoneAny({"spine_04", "spine_04.x"});
    spine[4] = a.FindBoneAny({"spine_05", "spine_05.x"});
    neck = a.FindBoneAny({"neck_01", "neck", "neck.x"});
    head = a.FindBoneAny({"head", "head.x"});
    const char* s[2] = {"l", "r"};
    for (int i = 0; i < 2; ++i) {
        const std::string S = s[i], L = i == 0 ? "left" : "right", U = i == 0 ? "L" : "R";
        auto f = [&](std::initializer_list<std::string> names) {
            int best = -1;
            for (const std::string& n : names) { best = a.FindBoneAny({n.c_str()}); if (best >= 0) break; }
            return best;
        };
        clavicle[i] = f({"clavicle_" + S, "shoulder_" + S, "shoulder." + S, L + "shoulder"});
        upperarm[i] = f({"upperarm_" + S, "arm_stretch." + S, "upper_arm." + S, L + "arm", "upperarm" + S});
        lowerarm[i] = f({"lowerarm_" + S, "forearm_stretch." + S, "forearm." + S, L + "forearm", "lowerarm" + S});
        hand[i] = f({"hand_" + S, "hand." + S, L + "hand"});
        thigh[i] = f({"thigh_" + S, "thigh_stretch." + S, "thigh." + S, L + "upleg"});
        calf[i] = f({"calf_" + S, "leg_stretch." + S, "shin." + S, L + "leg"});
        foot[i] = f({"foot_" + S, "foot." + S, L + "foot"});
        ball[i] = f({"ball_" + S, "toes_01." + S, "toe." + S, L + "toebase"});
        breast[i] = f({"breast_" + S, "breast." + S, "breast" + S, L + "breast", "boob_" + S, "boob." + S, "chest_" + S});
        glute[i] = f({"glute_" + S, "butt_" + S, "butt." + S, "buttock_" + S, "buttock." + S, L + "buttock", "hip_" + S});
        const char* fn[5] = {"thumb", "index", "middle", "ring", "pinky"};
        const char* mx[5] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
        for (int k = 0; k < 5; ++k)
            for (int j = 0; j < 3; ++j) {
                const std::string n = fn[k], J = std::to_string(j + 1);
                finger[i][k][j] = f({n + "_0" + J + "_" + S, n + J + "." + S, n + ".0" + J + "." + U, n + "_0" + J + "." + S,
                                     L + "hand" + mx[k] + J, (k == 4 ? std::string("little") : n) + "_0" + J + "_" + S});
            }
    }
}

int RigMap::SpineTop() const {
    for (int i = 4; i >= 0; --i) if (spine[i] >= 0) return spine[i];
    return pelvis;
}

// ---------------- Setup ----------------

bool Character::Init(const CharacterAsset* asset) {
    asset_ = asset;
    if (!asset_ || asset_->bones.empty()) return false;
    rig_.Build(*asset_);

    children_.assign(asset_->bones.size(), {});
    for (int i = 0; i < (int)asset_->bones.size(); ++i)
        if (asset_->bones[i].parent >= 0) children_[asset_->bones[i].parent].push_back(i);

    outPos_.resize(asset_->meshes.size());
    outNrm_.resize(asset_->meshes.size());
    for (size_t m = 0; m < asset_->meshes.size(); ++m) {
        outPos_[m] = asset_->meshes[m].pos;
        outNrm_[m] = asset_->meshes[m].nrm;
    }

    // Height and facing from the bind pose
    float lo = 1e9f, hi = -1e9f;
    for (const SkinMesh& m : asset_->meshes)
        if (m.garment < 0) for (const Vec3& p : m.pos) { lo = std::min(lo, p.y); hi = std::max(hi, p.y); }
    if (lo > hi) { lo = 0.f; hi = 1.7f; }
    height_ = hi - lo;

    placement = {};
    modelYaw = 0.f;
    ResetPose();
    Vec3 fwd{0, 0, 0};
    for (int i = 0; i < 2; ++i) {
        if (rig_.foot[i] >= 0 && rig_.ball[i] >= 0) fwd += world_[rig_.ball[i]].t - world_[rig_.foot[i]].t;
        if (rig_.breast[i] >= 0) {
            const int parent = asset_->bones[rig_.breast[i]].parent;
            if (parent >= 0) {
                // breasts sit in front of the spine
                Vec3 d = world_[rig_.breast[i]].t - world_[parent].t;
                d.y = 0;
                fwd += d;
            }
        }
    }
    if (rig_.thigh[0] >= 0 && rig_.thigh[1] >= 0 && fwd.Length() < 1e-4f) {
        // left thigh is on the character's left: forward = up x (left - right)... right-handed, Y up
        const Vec3 lr = world_[rig_.thigh[0]].t - world_[rig_.thigh[1]].t;
        fwd = lr.Cross({0, 1, 0}) * -1.f;
    }
    fwd.y = 0;
    if (fwd.Length() > 1e-4f) {
        fwd = fwd.Normalized();
        modelYaw = -std::atan2(fwd.x, fwd.z);   // rotate so forward maps to +Z
    }
    // Feet on the floor: lowest skin point, or the lowest bone if the mesh is only part of the body
    float boneLo = 1e9f;
    for (const Transform& w : world_) boneLo = std::min(boneLo, w.t.y);
    placement.t = {0, -std::min(lo, boneLo), 0};
    floorOffset = placement.t.y;

    ResetPose();
    FitCapsules();
    SetupSoftBones();
    SetupHands();
    BuildContactSamples();
    {
        // mouth: in front of and below the head joint; groin: below and in front of the hip joint (bind pose, model faces +Z)
        const Vec3 fwd = placement.r.Rotate({0, 0, 1});
        if (rig_.head >= 0) mouthLocal_ = world_[rig_.head].Inverse().Apply(world_[rig_.head].t + fwd * (0.085f * height_ / 1.7f) + Vec3{0, -0.045f * height_ / 1.7f, 0});
        if (rig_.pelvis >= 0) groinLocal_ = world_[rig_.pelvis].Inverse().Apply(world_[rig_.pelvis].t + fwd * (0.05f * height_ / 1.7f) + Vec3{0, -0.1f * height_ / 1.7f, 0});
    }
    limbDelta_.assign(4, Vec3{});
    return true;
}

Vec3 Character::BindPos(int bone) const {
    if (bone < 0) return {};
    const Mat4& m = asset_->bones[bone].bindWorld;
    const Vec3 p{m.m[12], m.m[13], m.m[14]};
    return placement.t + placement.r.Rotate(Quat::AxisAngle({0, 1, 0}, modelYaw).Rotate(p));
}

float Character::RefitFloor() {
    const Transform keep = placement;
    std::vector<Transform> keepLocal = local_;
    placement = {};
    placement.t = {0, 0, 0};
    ResetPose();
    Skin();
    float lo = 1e9f;
    for (size_t m = 0; m < asset_->meshes.size(); ++m) {
        if (asset_->meshes[m].garment >= 0) continue;
        for (const Vec3& p : outPos_[m]) lo = std::min(lo, p.y);
    }
    placement = keep;
    local_ = keepLocal;
    if (lo > 1e8f) return 0.f;
    const float old = floorOffset;
    floorOffset = -lo;
    placement.t.y += floorOffset - old;
    ComputeWorld();
    BuildContactSamples();   // foot shape changed with the morphs
    return floorOffset - old;
}

void Character::ResetPose() {
    local_.resize(asset_->bones.size());
    world_.resize(asset_->bones.size());
    for (size_t i = 0; i < asset_->bones.size(); ++i) local_[i] = asset_->bones[i].bindLocal;
    ComputeWorld();
}

void Character::ComputeWorld() {
    Transform base = placement;
    base.r = (placement.r * Quat::AxisAngle({0, 1, 0}, modelYaw)).Normalized();
    for (size_t i = 0; i < local_.size(); ++i) {
        const int p = asset_->bones[i].parent;
        world_[i] = (p >= 0 ? world_[p] : base) * local_[i];
    }
}

void Character::RecomputeSubtree(int bone) {
    const int p = asset_->bones[bone].parent;
    Transform base = placement;
    base.r = (placement.r * Quat::AxisAngle({0, 1, 0}, modelYaw)).Normalized();
    world_[bone] = (p >= 0 ? world_[p] : base) * local_[bone];
    for (int c : children_[bone]) RecomputeSubtree(c);
}

void Character::RotateBoneWorld(int bone, const Quat& delta) {
    if (bone < 0) return;
    const int p = asset_->bones[bone].parent;
    const Quat parentR = p >= 0 ? world_[p].r : (placement.r * Quat::AxisAngle({0, 1, 0}, modelYaw)).Normalized();
    const Quat newWorld = (delta * world_[bone].r).Normalized();
    local_[bone].r = (parentR.Conjugate() * newWorld).Normalized();
    RecomputeSubtree(bone);
}

void Character::SolveTwoBone(int a, int b, int c, const Vec3& target, const Vec3& pole, float weight, bool keepEndWorld) {
    if (a < 0 || b < 0 || c < 0 || weight <= 0.f) return;
    const Quat endRot = world_[c].r;
    const Vec3 A = world_[a].t, B = world_[b].t, C = world_[c].t;
    const float la = Distance(A, B), lb = Distance(B, C);
    if (la < 1e-5f || lb < 1e-5f) return;

    const Vec3 T = Lerp(C, target, std::clamp(weight, 0.f, 1.f));
    Vec3 toT = T - A;
    const float d = std::clamp(toT.Length(), std::fabs(la - lb) + 1e-4f, la + lb - 1e-4f);
    const Vec3 n = toT.Normalized((B - A).Normalized());

    Vec3 pd = pole - A;
    pd = pd - n * pd.Dot(n);
    if (pd.Length() < 1e-5f) { pd = (B - A) - n * (B - A).Dot(n); }
    pd = pd.Normalized({0, 1, 0});

    const float cosA = std::clamp((la * la + d * d - lb * lb) / (2.f * la * d), -1.f, 1.f);
    const float sinA = std::sqrt(std::max(0.f, 1.f - cosA * cosA));
    const Vec3 Bn = A + n * (cosA * la) + pd * (sinA * la);

    RotateBoneWorld(a, Quat::FromTo(B - A, Bn - A));
    RotateBoneWorld(b, Quat::FromTo(world_[c].t - world_[b].t, A + n * d - world_[b].t));

    if (!keepEndWorld) return;
    // Keep the hand/foot orientation the animation gave it
    const int pc = asset_->bones[c].parent;
    local_[c].r = (world_[pc].r.Conjugate() * endRot).Normalized();
    RecomputeSubtree(c);
}

// ---------------- Hands ----------------

void Character::SetupHands() {
    // Hand frame from the bind pose: fingers point from the knuckles to the tips, "across" runs pinky -> index.
    // Palm side by handedness (left: fingers x across, right: the reverse) so it holds for any bind pose.
    for (int i = 0; i < 2; ++i) {
        HandFrame& h = hands_[i];
        h = {};
        const int hb = rig_.hand[i];
        const auto& F = rig_.finger[i];
        if (hb < 0 || F[1][0] < 0 || F[4][0] < 0 || F[2][0] < 0 || F[2][2] < 0) continue;
        const Vec3 fingers = (world_[F[2][0]].t - world_[hb].t).Normalized();   // along the palm (wrist -> knuckle)
        Vec3 across = world_[F[1][0]].t - world_[F[4][0]].t;
        across = (across - fingers * across.Dot(fingers)).Normalized();
        const Vec3 palm = fingers.Cross(across) * (i == 0 ? 1.f : -1.f);
        const Quat inv = world_[hb].r.Conjugate();
        h.palmLocal = inv.Rotate(palm);
        h.fingerLocal = inv.Rotate(fingers);
        for (int k = 0; k < 5; ++k) {
            const int a = F[k][0];
            if (a < 0) continue;
            const int tip = F[k][2] >= 0 ? F[k][2] : F[k][1] >= 0 ? F[k][1] : a;
            Vec3 d = (world_[tip].t - world_[a].t).Normalized(fingers);
            if (tip == a) d = fingers;
            Vec3 axis;
            if (k == 0) {
                // the thumb folds across the palm toward the little finger
                axis = d.Cross((palm * 0.7f - across * 0.7f).Normalized()).Normalized(across);
            } else {
                axis = d.Cross(palm).Normalized(across);
            }
            for (int j = 0; j < 3; ++j)
                if (F[k][j] >= 0) h.curlAxis[k][j] = world_[F[k][j]].r.Conjugate().Rotate(axis);
            h.spreadAxis[k] = world_[a].r.Conjugate().Rotate(d.Cross(across).Normalized(palm));
        }
        h.valid = true;
    }
}

void Character::BuildContactSamples() {
    for (int i = 0; i < 2; ++i) {
        footSamples_[i].clear();
        const int fb = rig_.foot[i], bb = rig_.ball[i];
        if (fb < 0) continue;
        std::vector<Sample> all;
        for (const SkinMesh& m : asset_->meshes) {
            if (m.garment >= 0) continue;
            std::vector<Vec3> rest = m.pos;
            for (const Morph& mo : m.morphs) {
                const float w = GetMorph(mo.name);
                if (std::fabs(w) < 1e-4f) continue;
                for (size_t k = 0; k < mo.index.size(); ++k) rest[mo.index[k]] += mo.dpos[k] * w;
            }
            for (size_t v = 0; v < rest.size(); ++v) {
                int dom = m.joints[v][0];
                float dw = m.weights[v][0];
                for (int k = 1; k < 4; ++k) if (m.weights[v][k] > dw) { dw = m.weights[v][k]; dom = m.joints[v][k]; }
                if (dom != fb && dom != bb) continue;
                all.push_back({dom, asset_->bones[dom].invBind.Point(rest[v])});
            }
        }
        const size_t step = std::max<size_t>(1, all.size() / 120);
        for (size_t k = 0; k < all.size(); k += step) footSamples_[i].push_back(all[k]);
    }
}

float Character::LowestFootPoint(int i) const {
    float lo = 1e9f;
    int lastBone = -1;
    Mat4 M;
    for (const Sample& s : footSamples_[i]) {
        if (s.bone != lastBone) { M = Mat4::FromTransform(world_[s.bone]); lastBone = s.bone; }
        lo = std::min(lo, M.Point(s.local).y);
    }
    return footSamples_[i].empty() ? (rig_.foot[i] >= 0 ? world_[rig_.foot[i]].t.y - 0.07f : 0.f) : lo;
}

Vec3 Character::MouthPos() const { return rig_.head >= 0 ? world_[rig_.head].Apply(mouthLocal_) : placement.t + Vec3{0, 1.5f, 0}; }
Vec3 Character::GroinPos() const { return rig_.pelvis >= 0 ? world_[rig_.pelvis].Apply(groinLocal_) : placement.t + Vec3{0, 0.8f, 0}; }

Vec3 Character::PalmNormal(int i) const {
    const int hb = rig_.hand[i];
    if (hb < 0 || !hands_[i].valid) return {0, -1, 0};
    return world_[hb].r.Rotate(hands_[i].palmLocal);
}

Vec3 Character::FingerDir(int i) const {
    const int hb = rig_.hand[i];
    if (hb < 0 || !hands_[i].valid) return {0, -1, 0};
    return world_[hb].r.Rotate(hands_[i].fingerLocal);
}

void Character::OrientHand(int i, const Vec3& palmIn, const Vec3& fingersIn, float weight, float maxRad) {
    const int hb = rig_.hand[i];
    if (hb < 0 || !hands_[i].valid || weight <= 0.f) return;
    const Vec3 n0 = PalmNormal(i), f0 = FingerDir(i);
    const Vec3 n1 = palmIn.Normalized(n0);
    Vec3 f1 = fingersIn - n1 * fingersIn.Dot(n1);
    f1 = f1.Normalized(f0 - n1 * f0.Dot(n1));
    const Vec3 s0 = f0.Cross(n0).Normalized(), s1 = f1.Cross(n1).Normalized();
    const Quat cur = Quat::FromBasis(f0, s0.Cross(f0), s0);
    const Quat want = Quat::FromBasis(f1, s1.Cross(f1), s1);
    Quat d = (want * cur.Conjugate()).Normalized();
    Vec3 axis;
    float ang;
    d.ToAxisAngle(axis, ang);
    if (ang > kPi) ang -= 2.f * kPi;
    ang = std::clamp(ang * std::clamp(weight, 0.f, 1.f), -maxRad, maxRad);
    RotateBoneWorld(hb, Quat::AxisAngle(axis, ang));
}

void Character::PoseFingers(int i, float curl, float spread, float thumb, float indexCurl) {
    const HandFrame& h = hands_[i];
    if (!h.valid) return;
    const auto& F = rig_.finger[i];
    // knuckle, middle and end joints bend by different amounts (a real fist: ~80/100/70 degrees)
    const float jointK[3] = {1.35f, 1.65f, 1.1f};
    const float spreadK[5] = {0.f, 0.2f, 0.f, -0.14f, -0.28f};
    for (int k = 0; k < 5; ++k) {
        const float c = k == 0 ? thumb : (k == 1 && indexCurl >= 0.f) ? indexCurl : std::clamp(curl * (1.f + 0.06f * (k - 1)), -0.15f, 1.f);
        for (int j = 0; j < 3; ++j) {
            const int b = F[k][j];
            if (b < 0) continue;
            if (j == 0 && k > 0 && std::fabs(spread) > 1e-3f)
                RotateBoneWorld(b, Quat::AxisAngle(world_[b].r.Rotate(h.spreadAxis[k]), spread * spreadK[k] * (1.f - 0.6f * std::max(0.f, c))));
            const float a = k == 0 ? c * (j == 0 ? 0.55f : j == 1 ? 0.7f : 0.9f) : c * jointK[j];
            if (std::fabs(a) > 1e-4f) RotateBoneWorld(b, Quat::AxisAngle(world_[b].r.Rotate(h.curlAxis[k][j]), a));
        }
    }
}

// ---------------- Capsules fitted to the skin ----------------

void Character::FitCapsules() {
    capsules_.clear();
    // Bind-pose model space (no placement/yaw) for fitting
    std::vector<Transform> bw(asset_->bones.size());
    for (size_t i = 0; i < bw.size(); ++i) {
        const int p = asset_->bones[i].parent;
        bw[i] = p >= 0 ? bw[p] * asset_->bones[i].bindLocal : asset_->bones[i].bindLocal;
    }

    auto fit = [&](int bone, int child, int tag, int side, float minR, float maxR, float percentile) {
        if (bone < 0) return;
        BodyCapsule c;
        c.bone = bone;
        c.tag = tag;
        c.side = side;
        const Vec3 A = bw[bone].t;
        Vec3 B = child >= 0 ? bw[child].t : A;

        // gather skin strongly owned by this bone
        std::vector<Vec3> pts;
        for (const SkinMesh& m : asset_->meshes) {
            if (m.garment >= 0) continue;   // fit to skin, not clothes
            for (size_t v = 0; v < m.pos.size(); ++v)
                for (int k = 0; k < 4; ++k)
                    if (m.joints[v][k] == bone && m.weights[v][k] > 0.5f) { pts.push_back(m.pos[v]); break; }
        }

        if (child < 0 && !pts.empty()) {   // sphere-ish: from bone toward the tissue centroid
            Vec3 cen{};
            for (const Vec3& p : pts) cen += p;
            cen = cen * (1.f / pts.size());
            B = Lerp(A, cen, 0.6f);
        }
        std::vector<float> dist;
        dist.reserve(pts.size());
        for (const Vec3& p : pts) dist.push_back(Distance(p, ClosestOnSegment(p, A, B)));
        float r = minR;
        if (!dist.empty()) {
            std::nth_element(dist.begin(), dist.begin() + dist.size() * percentile, dist.end());
            r = dist[static_cast<size_t>(dist.size() * percentile)];
        }
        c.radius = std::clamp(r, minR, maxR);
        const Transform inv = bw[bone].Inverse();
        c.localA = inv.Apply(A);
        c.localB = inv.Apply(B);
        capsules_.push_back(c);
    };

    const int top = rig_.SpineTop();
    int chain[7] = {rig_.pelvis, rig_.spine[0], rig_.spine[1], rig_.spine[2], rig_.spine[3], rig_.spine[4], rig_.neck};
    int prev = -1;
    for (int b : chain) {
        if (b < 0) continue;
        if (prev >= 0) fit(prev, b, 0, -1, 0.05f, 0.18f, 0.55f);
        prev = b;
    }
    if (top >= 0 && rig_.neck < 0 && rig_.head >= 0) fit(top, rig_.head, 0, -1, 0.05f, 0.18f, 0.55f);
    if (rig_.neck >= 0 && rig_.head >= 0) fit(rig_.neck, rig_.head, 0, -1, 0.03f, 0.08f, 0.5f);
    if (rig_.head >= 0) fit(rig_.head, -1, 0, -1, 0.07f, 0.13f, 0.6f);
    for (int i = 0; i < 2; ++i) {
        fit(rig_.thigh[i], rig_.calf[i], 1, i, 0.04f, 0.11f, 0.55f);
        fit(rig_.breast[i], -1, 2, i, 0.03f, 0.09f, 0.45f);
        fit(rig_.glute[i], -1, 3, i, 0.04f, 0.10f, 0.45f);
    }
}

// ---------------- Soft tissue ----------------

void Character::SetupSoftBones() {
    soft_.clear();
    std::vector<Transform> bw(asset_->bones.size());
    for (size_t i = 0; i < bw.size(); ++i) {
        const int p = asset_->bones[i].parent;
        bw[i] = p >= 0 ? bw[p] * asset_->bones[i].bindLocal : asset_->bones[i].bindLocal;
    }
    auto add = [&](int bone, int tag) {
        if (bone < 0) return;
        SoftBone s;
        s.bone = bone;
        s.tag = tag;
        // direction/length from the bone to the centroid of the skin it owns
        Vec3 cen{};
        int n = 0;
        for (const SkinMesh& m : asset_->meshes) {
            if (m.garment >= 0) continue;
            for (size_t v = 0; v < m.pos.size(); ++v)
                for (int k = 0; k < 4; ++k)
                    if (m.joints[v][k] == bone && m.weights[v][k] > 0.3f) { cen += m.pos[v]; ++n; break; }
        }
        Vec3 d = n > 0 ? cen * (1.f / n) - bw[bone].t : Vec3{0, 0, 0.06f};
        // skin of this tissue (and its child bones, e.g. the nipple) for floor contact
        {
            std::vector<int> own = {bone};
            for (int c : children_[bone]) own.push_back(c);
            std::vector<Vec3> all;
            for (const SkinMesh& m : asset_->meshes) {
                if (m.garment >= 0) continue;
                for (size_t v = 0; v < m.pos.size(); ++v)
                    for (int k = 0; k < 4; ++k)
                        if (m.weights[v][k] > 0.45f && std::find(own.begin(), own.end(), (int)m.joints[v][k]) != own.end()) {
                            all.push_back(asset_->bones[bone].invBind.Point(m.pos[v]));
                            break;
                        }
            }
            const size_t stepS = std::max<size_t>(1, all.size() / 160);
            for (size_t k = 0; k < all.size(); k += stepS) s.samples.push_back(all[k]);
        }
        if (d.Length() < 0.01f) d = d.Normalized({0, 0, 1}) * 0.06f;
        s.length = std::clamp(d.Length() * 1.3f, 0.03f, 0.15f);
        s.localDir = bw[bone].r.Conjugate().Rotate(d.Normalized());
        if (tag == 3) {   // glutes: firmer, heavier
            s.frequency = 3.8f; s.damping = 0.38f; s.sag = 0.003f;
            s.maxOffset = {0.015f, 0.02f, 0.012f};
            s.transScale = 0.5f; s.rotScale = 0.5f;
        }
        soft_.push_back(s);
    };
    for (int i = 0; i < 2; ++i) add(rig_.breast[i], 2);
    for (int i = 0; i < 2; ++i) add(rig_.glute[i], 3);
}

void Character::SetSupport(int tag, float s) {
    for (SoftBone& b : soft_) if (b.tag == tag) b.support = std::clamp(s, 0.f, 1.f);
}

void Character::ResetDynamics() {
    for (SoftBone& b : soft_) b.valid = false;
    std::fill(limbDelta_.begin(), limbDelta_.end(), Vec3{});
}

void Character::UpdateSoftTissue(float dt) {
    dt = std::clamp(dt, 0.f, 0.1f);
    const int steps = std::clamp(static_cast<int>(std::ceil(dt * 240.f)), 1, 24);
    const float h = dt / steps;

    // Hands press soft tissue away
    Vec3 hands[2];
    bool hasHand[2] = {false, false};
    for (int i = 0; i < 2; ++i) if (rig_.hand[i] >= 0) { hands[i] = world_[rig_.hand[i]].t; hasHand[i] = true; }

    for (SoftBone& s : soft_) {
        const Transform& bwT = world_[s.bone];
        const Vec3 dirW = bwT.r.Rotate(s.localDir);
        const Vec3 rest = bwT.t + dirW * s.length;
        const float sup = s.support;
        const float w = 2.f * kPi * s.frequency * (1.f + 1.5f * sup);
        const float z = s.damping * (1.f + 0.5f * sup);
        const Vec3 sag = Vec3{0, -1, 0} * (s.sag * (1.f - 0.7f * sup));
        const Vec3 maxOff = s.maxOffset * (1.f - 0.6f * sup);

        // frame for range limits: along tissue, vertical, sideways
        const Vec3 F = dirW;
        Vec3 Up = Vec3{0, 1, 0} - F * F.y;
        Up = Up.Normalized({0, 0, 1});
        const Vec3 Side = F.Cross(Up);
        auto clampOff = [&](Vec3& off, Vec3& vel, float scale) {
            float c[3] = {off.Dot(Side), off.Dot(Up), off.Dot(F)};
            float v[3] = {vel.Dot(Side), vel.Dot(Up), vel.Dot(F)};
            const float m[3] = {maxOff.x * scale, maxOff.y * scale, maxOff.z * scale};
            for (int k = 0; k < 3; ++k) {
                if (c[k] > m[k]) { c[k] = m[k]; v[k] = std::min(v[k], 0.f); }
                if (c[k] < -m[k]) { c[k] = -m[k]; v[k] = std::max(v[k], 0.f); }
            }
            off = Side * c[0] + Up * c[1] + F * c[2];
            vel = Side * v[0] + Up * v[1] + F * v[2];
        };

        if (!s.valid || Distance(rest, s.lastRest) > 0.5f) {
            s.pos = rest + sag;
            s.vel = {};
            s.lastRest = rest;
            s.valid = true;
        }
        for (int k = 0; k < steps; ++k) {
            const Vec3 r = Lerp(s.lastRest, rest, float(k + 1) / steps);
            const Vec3 target = r + sag;
            const Vec3 acc = (target - s.pos) * (w * w) - s.vel * (2.f * z * w);
            s.vel += acc * h;
            s.pos += s.vel * h;
            Vec3 off = s.pos - target;
            clampOff(off, s.vel, 1.f);
            s.pos = target + off;
        }
        for (int i = 0; i < 2; ++i) {
            if (!hasHand[i]) continue;
            Vec3 p = s.pos;
            if (PushOut(p, s.length * 0.6f, hands[i], hands[i], 0.035f)) {
                const Vec3 n = (p - s.pos).Normalized();
                s.pos = p;
                const float into = s.vel.Dot(n);
                if (into < 0) s.vel -= n * into;
            }
        }
        {
            Vec3 off = s.pos - (rest + sag);
            clampOff(off, s.vel, 1.5f);
            s.pos = rest + sag + off;
        }
        s.lastRest = rest;

        const Vec3 disp = s.pos - rest;
        if (disp.Length() < 1e-5f) continue;
        // translate (in parent space) and rotate toward the mass point
        const int p = asset_->bones[s.bone].parent;
        const Quat parentR = p >= 0 ? world_[p].r : placement.r;
        const Vec3 ps = p >= 0 ? world_[p].s : Vec3{1, 1, 1};
        const Vec3 dl = parentR.Conjugate().Rotate(disp * s.transScale);
        local_[s.bone].t += Vec3{dl.x / ps.x, dl.y / ps.y, dl.z / ps.z};
        RecomputeSubtree(s.bone);
        const Quat q = Quat::FromTo(dirW * s.length, dirW * s.length + disp);
        RotateBoneWorld(s.bone, Nlerp(Quat{}, q, s.rotScale));
    }
    ApplyTissueContact(dt);
}

void Character::ApplyTissueContact(float dt) {
    // Pressed into the floor/bed, tissue flattens against the body and spreads out (volume kept roughly constant).
    // Squash is about the bone origin at the chest wall / hip, along whichever bone axis points most at the surface.
    const float k = std::clamp(dt * 25.f, 0.f, 1.f);
    for (SoftBone& s : soft_) {
        float want = 1.f;
        int axis = 1;
        if (!s.samples.empty() && contactFloorY > -1e8f) {
            const Mat4 M = Mat4::FromTransform(world_[s.bone]);
            float lo = 1e9f;
            for (const Vec3& p : s.samples) lo = std::min(lo, M.Point(p).y);
            const float boneY = world_[s.bone].t.y;
            const float surf = contactFloorY + 0.004f;
            if (lo < surf && boneY - lo > 1e-3f) want = std::clamp((boneY - surf) / (boneY - lo), 0.3f, 1.f);
            const Vec3 upL = world_[s.bone].r.Conjugate().Rotate({0, 1, 0});
            const float a[3] = {std::fabs(upL.x), std::fabs(upL.y), std::fabs(upL.z)};
            axis = a[0] > a[1] ? (a[0] > a[2] ? 0 : 2) : (a[1] > a[2] ? 1 : 2);
        }
        // squash instantly when pressed harder (the floor is solid), recover with a little lag
        s.squash = want < s.squash ? want : s.squash + (want - s.squash) * k;
        s.pressure = std::clamp((1.f - s.squash) / 0.5f, 0.f, 1.f);
        if (s.squash > 0.999f) continue;
        const float spread = std::min(1.35f, 1.f / std::sqrt(s.squash));
        Vec3 sc{spread, spread, spread};
        (axis == 0 ? sc.x : axis == 1 ? sc.y : sc.z) = s.squash;
        Vec3& ls = local_[s.bone].s;
        ls = Vec3{ls.x * sc.x, ls.y * sc.y, ls.z * sc.z};
        RecomputeSubtree(s.bone);
        // the spring's mass point can't sit inside the floor either
        if (s.vel.y < 0.f) s.vel.y *= 0.3f;
    }
}

// ---------------- Limb collision ----------------

void Character::UpdateLimbCollision(float dt) {
    for (int side = 0; side < 2; ++side) {
        const int a = rig_.upperarm[side], b = rig_.lowerarm[side], c = rig_.hand[side];
        if (a < 0 || b < 0 || c < 0) continue;
        const Vec3 A = world_[a].t, B0 = world_[b].t, C0 = world_[c].t;
        Vec3 B = B0, C = C0;
        const float handR = 0.035f, elbowR = 0.035f, foreR = 0.03f;

        for (int it = 0; it < 3; ++it) {
            bool any = false;
            for (const BodyCapsule& cap : capsules_) {
                Vec3 p0, p1;
                CapsuleWorld(cap, p0, p1);
                any |= PushOut(C, handR, p0, p1, cap.radius);
                any |= PushOut(B, elbowR, p0, p1, cap.radius);
                Vec3 mid = (B + C) * 0.5f;
                const Vec3 before = mid;
                if (PushOut(mid, foreR, p0, p1, cap.radius)) {
                    const Vec3 push = mid - before;
                    B += push;
                    C += push;
                    any = true;
                }
            }
            if (!any) break;
        }

        // entering the body snaps out instantly; leaving relaxes
        auto smooth = [&](Vec3& prev, const Vec3& want) {
            if (want.Length() >= prev.Length()) prev = want;
            else prev = Lerp(prev, want, std::clamp(dt * 12.f, 0.f, 1.f));
        };
        smooth(limbDelta_[side * 2 + 0], C - C0);
        smooth(limbDelta_[side * 2 + 1], B - B0);
        if (limbDelta_[side * 2].Length() < 1e-4f && limbDelta_[side * 2 + 1].Length() < 1e-4f) continue;

        const Vec3 mid = (A + C0) * 0.5f;
        const Vec3 bend = (B0 - mid).Normalized({0, 0, -1});
        SolveTwoBone(a, b, c, C0 + limbDelta_[side * 2], B0 + bend * 0.2f + limbDelta_[side * 2 + 1]);
    }
}

// ---------------- Morphs ----------------

void Character::SetMorph(const std::string& name, float weight) { morphWeights_[name] = weight; }

float Character::GetMorph(const std::string& name) const {
    auto it = morphWeights_.find(name);
    return it == morphWeights_.end() ? 0.f : it->second;
}

std::vector<std::string> Character::MorphNames() const {
    std::vector<std::string> out;
    for (const SkinMesh& m : asset_->meshes)
        for (const Morph& mo : m.morphs)
            if (std::find(out.begin(), out.end(), mo.name) == out.end()) out.push_back(mo.name);
    return out;
}

// ---------------- Skinning ----------------

void Character::SetSkinMask(const std::vector<bool>& drawn) {
    skinMask_.assign(asset_->meshes.size(), 1);
    for (size_t i = 0; i < drawn.size() && i < skinMask_.size(); ++i) skinMask_[i] = drawn[i] ? 1 : 0;
}

void Character::SetMeshOffset(size_t mesh, const Vec3& offset) {
    if (meshOffset_.size() < asset_->meshes.size()) meshOffset_.resize(asset_->meshes.size());
    if (mesh < meshOffset_.size()) meshOffset_[mesh] = offset;
}

void Character::Skin() {
    // 3x4 skinning matrices (bone world * inverse bind)
    const size_t nb = asset_->bones.size();
    skinMats_.resize(nb);
    for (size_t i = 0; i < nb; ++i) skinMats_[i] = Mat4::FromTransform(world_[i]) * asset_->bones[i].invBind;
    const size_t nm = asset_->meshes.size();
    if (morphBase_.size() != nm) { morphBase_.assign(nm, {}); morphW_.assign(nm, {}); }

    // morphs (rest space), cached: most frames only a few face meshes change
    struct Range { size_t mesh, begin, end; };
    std::vector<Range> todo;
    for (size_t mi = 0; mi < nm; ++mi) {
        if (!skinMask_.empty() && !skinMask_[mi]) continue;
        const SkinMesh& m = asset_->meshes[mi];
        std::vector<float>& wv = morphW_[mi];
        std::vector<Vec3>& base = morphBase_[mi];
        const bool fresh = wv.size() != m.morphs.size() || base.size() != m.pos.size() || ++morphEdits_ > 4000;
        if (fresh) {   // rebuild from rest (first time, and now and then so float drift can't build up)
            if (morphEdits_ > 4000) morphEdits_ = 0;
            wv.assign(m.morphs.size(), 0.f);
            base = m.pos;
        }
        // only the morphs whose weight moved are applied, by how much they moved (a blink touches a few hundred verts)
        for (size_t k = 0; k < m.morphs.size(); ++k) {
            float w = GetMorph(m.morphs[k].name);
            if (std::fabs(w) < 1e-4f) w = 0.f;
            const float dw = w - wv[k];
            if (std::fabs(dw) <= 1e-5f) continue;
            const Morph& mo = m.morphs[k];
            for (size_t q = 0; q < mo.index.size(); ++q) base[mo.index[q]] += mo.dpos[q] * dw;
            wv[k] = w;
            ++morphEdits_;
        }
        todo.push_back({mi, 0, m.pos.size()});
    }
    // one flat job over every vertex of every drawn mesh, on the worker pool
    std::vector<size_t> start(todo.size() + 1, 0);
    for (size_t t = 0; t < todo.size(); ++t) start[t + 1] = start[t] + (todo[t].end - todo[t].begin);
    ParallelFor(start.back(), 2048, [&](size_t b, size_t e) {
        size_t t = std::upper_bound(start.begin(), start.end(), b) - start.begin() - 1;
        while (b < e && t < todo.size()) {
            const size_t mi = todo[t].mesh;
            const SkinMesh& m = asset_->meshes[mi];
            const std::vector<Vec3>& src = morphBase_[mi];
            std::vector<Vec3>& op = outPos_[mi];
            std::vector<Vec3>& on = outNrm_[mi];
            const size_t v0 = b - start[t], v1 = std::min(e, start[t + 1]) - start[t];
            const Vec3 shift = mi < meshOffset_.size() ? meshOffset_[mi] : Vec3{};
            for (size_t v = v0; v < v1; ++v) {
                // blend the bone matrices first, then transform once
                const auto& j = m.joints[v];
                const auto& w = m.weights[v];
                float M[12] = {};
                for (int k = 0; k < 4; ++k) {
                    if (w[k] <= 0.f) continue;
                    const float* s = skinMats_[j[k]].m;
                    const float wk = w[k];
                    M[0] += s[0] * wk; M[1] += s[1] * wk; M[2] += s[2] * wk;
                    M[3] += s[4] * wk; M[4] += s[5] * wk; M[5] += s[6] * wk;
                    M[6] += s[8] * wk; M[7] += s[9] * wk; M[8] += s[10] * wk;
                    M[9] += s[12] * wk; M[10] += s[13] * wk; M[11] += s[14] * wk;
                }
                const Vec3& p = src[v];
                const Vec3& n = m.nrm[v];
                op[v] = Vec3{M[0] * p.x + M[3] * p.y + M[6] * p.z + M[9], M[1] * p.x + M[4] * p.y + M[7] * p.z + M[10], M[2] * p.x + M[5] * p.y + M[8] * p.z + M[11]} + shift;
                const Vec3 nn{M[0] * n.x + M[3] * n.y + M[6] * n.z, M[1] * n.x + M[4] * n.y + M[7] * n.z, M[2] * n.x + M[5] * n.y + M[8] * n.z};
                const float l2 = nn.x * nn.x + nn.y * nn.y + nn.z * nn.z;
                on[v] = l2 > 1e-20f ? nn * (1.f / std::sqrt(l2)) : Vec3{0, 1, 0};
            }
            b = start[t + 1];
            ++t;
        }
    });
}

} // namespace eon
