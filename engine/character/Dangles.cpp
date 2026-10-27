#include "Dangles.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace eon {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool HasPrefix(const std::string& name, const std::vector<std::string>& prefixes) {
    const std::string n = Lower(name);
    for (const std::string& p : prefixes) if (n.rfind(Lower(p), 0) == 0) return true;
    return false;
}

Vec3 ClosestOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const float l2 = ab.Dot(ab);
    if (l2 < 1e-10f) return a;
    const float t = std::clamp((p - a).Dot(ab) / l2, 0.f, 1.f);
    return a + ab * t;
}

} // namespace

size_t DangleSim::BoneCount() const {
    size_t n = 0;
    for (const Chain& c : chains_) n += c.bones.size();
    return n;
}

void DangleSim::Init(Character& ch, const std::vector<std::string>& prefixes, const DangleSettings& s) {
    settings = s;
    chains_.clear();
    const CharacterAsset& a = *ch.Asset();
    ch.ResetPose();
    const auto& w = ch.World();

    std::vector<std::vector<int>> kids(a.bones.size());
    for (int i = 0; i < (int)a.bones.size(); ++i)
        if (a.bones[i].parent >= 0) kids[a.bones[i].parent].push_back(i);

    for (int i = 0; i < (int)a.bones.size(); ++i) {
        if (!HasPrefix(a.bones[i].name, prefixes)) continue;
        const int p = a.bones[i].parent;
        if (p >= 0 && HasPrefix(a.bones[p].name, prefixes)) continue;   // not a chain root
        // A root may branch (ponytail core with side strands): every branch becomes its own chain
        std::vector<std::vector<int>> stack = {{i}};
        while (!stack.empty()) {
            std::vector<int> chain = stack.back();
            stack.pop_back();
            for (;;) {
                const int last = chain.back();
                std::vector<int> next;
                for (int k : kids[last]) if (HasPrefix(a.bones[k].name, prefixes)) next.push_back(k);
                if (next.empty()) break;
                for (size_t b = 1; b < next.size(); ++b) stack.push_back({next[b]});   // side branch starts fresh
                chain.push_back(next[0]);
            }
            Chain c;
            c.bones = chain;
            for (size_t k = 0; k + 1 < chain.size(); ++k) c.len.push_back(std::max(0.005f, Distance(w[chain[k]].t, w[chain[k + 1]].t)));
            // virtual tip: continue the last segment's direction (or the bone's own axis for 1-bone chains)
            const int last = chain.back();
            Vec3 dir = chain.size() > 1 ? (w[last].t - w[chain[chain.size() - 2]].t) : w[last].r.Rotate({0, 1, 0});
            const float tipLen = chain.size() > 1 ? c.len.back() : 0.05f;
            const Vec3 tipW = w[last].t + dir.Normalized({0, -1, 0}) * tipLen;
            c.len.push_back(tipLen);
            c.localTip.push_back(w[last].Inverse().Apply(tipW));
            chains_.push_back(std::move(c));
        }
    }
}

void DangleSim::Update(Character& ch, float dt) {
    dt = std::clamp(dt, 0.f, 0.05f);
    if (dt <= 0.f) return;
    const int steps = std::clamp(static_cast<int>(std::ceil(dt * 120.f)), 1, 6);
    const float h = dt / steps;
    const Vec3 g = Vec3{0, -9.81f, 0} * settings.gravityScale;

    // Colliders: head/neck/torso/breast capsules
    struct Col { Vec3 a, b; float r; };
    std::vector<Col> cols;
    const RigMap& rig = ch.Rig();
    for (const BodyCapsule& c : ch.Capsules()) {
        if (c.tag == 1 || c.tag == 3) continue;   // thighs/glutes: skirts handle those later
        Col k;
        ch.CapsuleWorld(c, k.a, k.b);
        // torso capsules are fitted generously (they include the chest); hair rests on skin, so slim them
        const bool headNeck = c.bone == rig.head || c.bone == rig.neck;
        k.r = c.tag == 0 && !headNeck ? c.radius * 0.7f : c.radius * 0.95f;
        cols.push_back(k);
    }

    for (Chain& c : chains_) {
        const auto& w = ch.World();
        const size_t n = c.bones.size();
        // animated (styled) target positions
        std::vector<Vec3> goal(n + 1);
        for (size_t i = 0; i < n; ++i) goal[i] = w[c.bones[i]].t;
        goal[n] = w[c.bones.back()].Apply(c.localTip[0]);

        if (!c.valid || Distance(c.pos[0], goal[0]) > 0.5f) {
            c.pos = goal;
            c.prev = goal;
            c.valid = true;
        }

        for (int s = 0; s < steps; ++s) {
            c.pos[0] = goal[0];   // root follows the head
            c.prev[0] = goal[0];
            for (size_t i = 1; i <= n; ++i) {
                const Vec3 v = (c.pos[i] - c.prev[i]) * (1.f - settings.damping);
                c.prev[i] = c.pos[i];
                c.pos[i] += v + g * (h * h);
                // styled shape holds near the scalp, tips hang free (real hair falls when she bends over)
                const float k = settings.stiffness * (1.f - 0.85f * float(i) / float(n));
                c.pos[i] += (goal[i] - c.pos[i]) * k;
            }
            for (int it = 0; it < 2; ++it) {
                for (size_t i = 1; i <= n; ++i) {
                    // inextensible strand
                    Vec3 d = c.pos[i] - c.pos[i - 1];
                    const float L = d.Length();
                    if (L > 1e-6f) c.pos[i] = c.pos[i - 1] + d * (c.len[i - 1] / L);
                    // stay outside the body
                    for (const Col& k : cols) {
                        const Vec3 q = ClosestOnSegment(c.pos[i], k.a, k.b);
                        Vec3 off = c.pos[i] - q;
                        const float minD = k.r + settings.radius;
                        const float l = off.Length();
                        if (l < minD) c.pos[i] = q + off.Normalized({0, 0, -1}) * minD;
                    }
                }
            }
        }

        // Rotate bones to follow the particles
        for (size_t i = 0; i < n; ++i) {
            const auto& wn = ch.World();
            const Vec3 from = (i + 1 < n ? wn[c.bones[i + 1]].t : wn[c.bones[i]].Apply(c.localTip[0])) - wn[c.bones[i]].t;
            const Vec3 to = c.pos[i + 1] - wn[c.bones[i]].t;
            if (from.Length() < 1e-5f || to.Length() < 1e-5f) continue;
            ch.RotateBoneWorld(c.bones[i], Quat::FromTo(from, to));
        }
    }
}

} // namespace eon
