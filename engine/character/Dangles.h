#pragma once

#include <string>
#include <vector>

#include "Character.h"

namespace eon {

// Hair strands, ponytail, earrings, skirt panels: bone chains simulated as verlet particles that hang
// under gravity, swing with her motion, keep a little of their styled shape, and collide with her head,
// neck and torso.
struct DangleSettings {
    float stiffness = 0.05f;   // pull back toward the styled (animated) shape per step, 0..1
    float damping = 0.06f;     // velocity loss per step
    float gravityScale = 1.f;
    float radius = 0.012f;     // collision thickness of the strand (m)
};

class DangleSim {
public:
    // Chains are found from bone name prefixes, e.g. {"hair_", "earring_"}.
    void Init(Character& ch, const std::vector<std::string>& prefixes, const DangleSettings& s = {});
    void Update(Character& ch, float dt);
    void Reset() { for (Chain& c : chains_) c.valid = false; }
    size_t ChainCount() const { return chains_.size(); }
    size_t BoneCount() const;

    DangleSettings settings;

private:
    struct Chain {
        std::vector<int> bones;        // root .. last
        std::vector<float> len;        // segment lengths (bones.size() segments, last one to a virtual tip)
        std::vector<Vec3> localTip;    // last bone's tip in its local space
        std::vector<Vec3> pos, prev;   // particles: index i = start of bone i; back() = tip
        bool valid = false;
    };
    std::vector<Chain> chains_;
};

} // namespace eon
