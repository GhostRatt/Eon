#pragma once

#include <random>

#include "Character.h"

namespace eon {

// What the face reacts to (all 0..1 unless noted). Filled from the body state each frame.
struct FaceInputs {
    float mood = 0.7f;       // 0 miserable .. 1 great
    float fatigue = 0.2f;    // heavy lids, slow blinks
    float cold = 0.f;        // tense, pressed lips
    float hot = 0.f;         // squint, slightly open mouth
    float breath = 0.f;      // 0..1 breathing phase (exhale opens the mouth a touch when out of breath)
    float effort = 0.f;      // exertion / out of breath
    float asleep = 0.f;      // eyes closed
    Vec3 lookAt;             // world point to look at
    bool hasLookAt = false;
};

// Natural blinking, eye movement and mood expressions on ARKit-style morph targets
// (EyesBlink/EyeBlinkLeft, eyeLook*, mouthSmile, browInnerUp, jawOpen...). Missing morphs are skipped.
class FaceSim {
public:
    void Init(Character& ch, unsigned seed = 7);
    void Update(Character& ch, const FaceInputs& in, float dt);
    float Blink() const { return blink_; }

private:
    void Set(Character& ch, const char* name, float w);
    std::mt19937 rng_;
    float nextBlink_ = 2.f, blinkT_ = -1.f, blink_ = 0.f;
    bool doubleBlink_ = false;
    float saccadeT_ = 0.f;
    float lookX_ = 0.f, lookY_ = 0.f, lookTX_ = 0.f, lookTY_ = 0.f;
    float smile_ = 0.f, frown_ = 0.f, brow_ = 0.f, squint_ = 0.f, jaw_ = 0.f, press_ = 0.f;
    std::vector<std::string> available_;
};

} // namespace eon
