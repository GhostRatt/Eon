#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "character/BodyMatter.h"
#include "character/BodyState.h"
#include "character/Character.h"
#include "character/Motor.h"
#include "core/World.h"

namespace eon {

// The sensor firewall. Everything Jenny's mind gets passes through here: what her eyes can see from her head,
// what her hands feel, what her body feels. No entity names, types, ids or engine state ever cross it —
// things are opaque tracks (like recognizing "that same red bump again") with only what senses can tell.
struct SeenThing {
    uint32_t track = 0;     // stable while it stays recognizable; meaningless outside her mind
    Vec3 pos;               // center
    Vec3 lo, hi;            // visible extent
    Rgb color;
    float glow = 0.f;       // how bright it shines (0 none)
    float dist = 0.f;
    bool wide = false;      // floor/wall-sized surface
};

struct HandFeel {
    uint32_t touching = 0;  // track under the palm/fingertip, 0 none
    bool holding = false;
    uint32_t held = 0;
    float weight = 0.f;     // kg felt in this hand
    float force = 0.f;      // N she is pushing/pulling with
    bool blocked = false;   // can't get there
    bool pointing = false;
    bool reaching = false;  // she is moving this hand on purpose (she knows her own intentions)
};

struct Percept {
    double t = 0.0;                 // game seconds
    Vec3 eye, self;                 // where she is (her own body: proprioception, not a world coordinate she's told)
    float yaw = 0.f;
    std::vector<SeenThing> seen;
    std::array<HandFeel, 2> hands;
    Stance stance = Stance::Stand;
    bool bumped = false, atEdge = false;
    float light = 0.f;              // overall brightness she sees
    std::vector<std::pair<std::string, float>> feelings;   // ("cold", 0.7) ...
    // mouth and body (what she takes in / lets out, felt directly)
    float drankMl = 0.f, biteG = 0.f;
    bool chewing = false, peeing = false;
    float airC = 20.f;              // how warm the air feels where she is
};

class Perception {
public:
    float fovDeg = 160.f;   // incl. peripheral vision (a light coming on at the side is noticed)
    float range = 7.f;
    void Sense(const World& world, const Motor& motor, const Character& ch, const BodyState* body, double t, Percept& out,
               const BodyMatterFeel* matter = nullptr);

private:
    uint32_t Track(int entity);
    std::unordered_map<uint64_t, uint32_t> trackOf_;   // stays on this side of the firewall
    uint64_t zoneKey_ = 0;
    uint32_t next_ = 1;
    RenderFrame frame_;
};

} // namespace eon
