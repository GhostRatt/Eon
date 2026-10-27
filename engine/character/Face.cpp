#include "Face.h"

#include <algorithm>
#include <cmath>

namespace eon {

namespace {
float Approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * std::clamp(rate * dt, 0.f, 1.f);
}
}

void FaceSim::Init(Character& ch, unsigned seed) {
    rng_.seed(seed);
    available_ = ch.MorphNames();
    nextBlink_ = 1.f + std::uniform_real_distribution<float>(0.f, 3.f)(rng_);
}

void FaceSim::Set(Character& ch, const char* name, float w) {
    if (std::find(available_.begin(), available_.end(), name) == available_.end()) return;
    ch.SetMorph(name, std::clamp(w, 0.f, 1.f));
}

void FaceSim::Update(Character& ch, const FaceInputs& in, float dt) {
    std::uniform_real_distribution<float> U(0.f, 1.f);

    // ---- Blinking: ~15-20/min awake, longer and lazier when tired; closed when asleep ----
    nextBlink_ -= dt;
    if (blinkT_ < 0.f && nextBlink_ <= 0.f && in.asleep < 0.5f) {
        blinkT_ = 0.f;
        doubleBlink_ = U(rng_) < 0.12f;
        const float mean = 3.2f + 1.5f * in.fatigue;
        nextBlink_ = std::max(0.4f, -std::log(std::max(1e-4f, U(rng_))) * mean);   // exponential gaps
    }
    float blinkClose = 0.f;
    if (blinkT_ >= 0.f) {
        const float dur = 0.16f + 0.18f * in.fatigue;
        blinkT_ += dt;
        const float ph = blinkT_ / dur;
        blinkClose = ph < 0.35f ? ph / 0.35f : std::max(0.f, 1.f - (ph - 0.35f) / 0.65f);   // fast close, slower open
        if (ph >= 1.f) {
            if (doubleBlink_) { doubleBlink_ = false; blinkT_ = -0.f; }
            else blinkT_ = -1.f;
        }
    }
    const float lids = std::max({blinkClose, 0.35f * in.fatigue * in.fatigue, in.asleep});
    blink_ = lids;
    Set(ch, "EyesBlink", lids);

    // ---- Eyes: small saccades, or toward a look target ----
    saccadeT_ -= dt;
    if (saccadeT_ <= 0.f) {
        saccadeT_ = 0.4f + U(rng_) * 2.2f;
        lookTX_ = (U(rng_) - 0.5f) * 0.5f;
        lookTY_ = (U(rng_) - 0.5f) * 0.3f;
    }
    if (in.hasLookAt && ch.Rig().head >= 0) {
        const Transform& h = ch.World()[ch.Rig().head];
        const Vec3 local = h.r.Conjugate().Rotate(in.lookAt - h.t).Normalized({0, 0, 1});
        // head local axes vary per rig; use world-ish yaw/pitch relative to the character's facing
        const Vec3 fwd = ch.placement.r.Rotate({0, 0, 1});
        const Vec3 d = (in.lookAt - h.t).Normalized(fwd);
        const Vec3 left = Vec3{0, 1, 0}.Cross(fwd).Normalized({1, 0, 0});
        lookTX_ = std::clamp(d.Dot(left), -1.f, 1.f);
        lookTY_ = std::clamp(d.y, -1.f, 1.f);
        (void)local;
    }
    lookX_ = Approach(lookX_, lookTX_, 25.f, dt);   // saccades are fast
    lookY_ = Approach(lookY_, lookTY_, 25.f, dt);
    Set(ch, "eyeLookOutLeft", std::max(0.f, lookX_));
    Set(ch, "eyeLookInRight", std::max(0.f, lookX_));
    Set(ch, "eyeLookInLeft", std::max(0.f, -lookX_));
    Set(ch, "eyeLookOutRight", std::max(0.f, -lookX_));
    Set(ch, "eyeLookUpLeft", std::max(0.f, lookY_));
    Set(ch, "eyeLookUpRight", std::max(0.f, lookY_));
    Set(ch, "eyeLookDownLeft", std::max(0.f, -lookY_) + 0.2f * in.fatigue);
    Set(ch, "eyeLookDownRight", std::max(0.f, -lookY_) + 0.2f * in.fatigue);

    // ---- Expression from mood and body ----
    smile_ = Approach(smile_, std::clamp((in.mood - 0.65f) * 2.f, 0.f, 0.6f), 1.5f, dt);
    frown_ = Approach(frown_, std::clamp((0.4f - in.mood) * 2.f, 0.f, 0.7f), 1.5f, dt);
    brow_ = Approach(brow_, std::clamp((0.45f - in.mood) * 1.8f, 0.f, 0.8f) + 0.3f * in.cold, 1.5f, dt);
    squint_ = Approach(squint_, 0.5f * in.hot, 1.f, dt);
    press_ = Approach(press_, 0.6f * in.cold, 1.f, dt);
    const float breathOpen = in.effort * (0.5f + 0.5f * std::sin(in.breath * 6.2831853f)) * 0.35f + 0.08f * in.hot;
    jaw_ = Approach(jaw_, std::min(0.4f, breathOpen) * (1.f - in.asleep * 0.5f), 6.f, dt);

    Set(ch, "mouthSmile", smile_);
    Set(ch, "cheekSquintLeft", smile_ * 0.4f + squint_ * 0.3f);
    Set(ch, "cheekSquintRight", smile_ * 0.4f + squint_ * 0.3f);
    Set(ch, "mouthFrownLeft", frown_);
    Set(ch, "mouthFrownRight", frown_);
    Set(ch, "browInnerUp", brow_);
    Set(ch, "browDownLeft", squint_ * 0.6f);
    Set(ch, "browDownRight", squint_ * 0.6f);
    Set(ch, "eyeSquintLeft", squint_);
    Set(ch, "eyeSquintRight", squint_);
    Set(ch, "mouthPressLeft", press_);
    Set(ch, "mouthPressRight", press_);
    Set(ch, "jawOpen", jaw_);
}

} // namespace eon
