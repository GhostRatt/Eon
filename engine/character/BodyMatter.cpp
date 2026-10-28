#include "BodyMatter.h"

#include <algorithm>
#include <cmath>

namespace eon {

void BodyMatter::Step(World& world, const Character& ch, const Motor& motor, const MotorIntent& in, BodyState& body,
                      bool groinCovered, float dt) {
    feel_ = {};
    const Vec3 mouth = ch.MouthPos();

    // mouth: open lets liquid in; biting closes the jaw on what's in front of it, then she chews
    biteCool_ = std::max(0.f, biteCool_ - dt);
    float wantJaw = 0.f;
    if (in.mouth == MouthAction::Open) wantJaw = 0.55f;
    if (in.mouth == MouthAction::Bite && chew_ <= 0.f) {
        wantJaw = biteCool_ > 0.35f ? 0.f : 0.7f;   // open wide, then snap shut
        if (biteCool_ <= 0.f && jaw_ > 0.6f) {
            const float g = world.Bite(mouth, 0.06f);
            biteCool_ = 0.6f;
            if (g > 0.f) { chew_ = 0.8f + g * 0.08f; feel_.biteG = g; }
        }
    }
    if (chew_ > 0.f) {   // chewing: the jaw works up and down, the mouth stays closed to liquids
        chew_ -= dt;
        wantJaw = 0.18f * (0.5f + 0.5f * std::sin(chew_ * 18.f));
        feel_.chewing = true;
    }
    jaw_ += (wantJaw - jaw_) * std::clamp(dt * 14.f, 0.f, 1.f);
    world.SetMouth(mouth, in.mouth == MouthAction::Open && chew_ <= 0.f);

    // peeing: from the groin, down (and a little forward); standing it runs down her legs too
    const bool canPee = in.pee && body.BladderMl() > 2.f;
    if (canPee) {
        const Vec3 F = ch.placement.r.Rotate({0, 0, 1});
        const Stance st = motor.CurrentStance();
        const bool seated = st == Stance::SitSeat || st == Stance::Crouch;
        Vec3 vel = Vec3{0, -0.35f, 0} + F * (seated ? 0.12f : 0.25f);
        float rate = std::min(peeRate, body.BladderMl() / dt);
        float onBody = 0.f;
        if (groinCovered) { onBody += rate * 0.6f; feel_.soakedClothes = true; }   // soaks through her underwear first
        if (st == Stance::Stand) onBody += rate * 0.3f;                               // down the inside of her legs
        onBody = std::min(onBody, rate);
        world.SetBodyStream(ch.GroinPos() + F * 0.02f, vel, rate - onBody, Liquid::Urine);
        body.Urinate(rate * dt);
        body.WetSkin(onBody * dt, true);
        feel_.peeing = true;
    } else {
        world.ClearBodyStream();
    }

    // standing (or lying, or sitting) in water: wet as high as it comes, drinks if her open mouth is in it
    {
        const RigMap& rig = ch.Rig();
        const Vec3 head = ch.BonePos(rig.head) + Vec3{0, 0.1f, 0};
        float footY = std::min(ch.BonePos(rig.foot[0]).y, ch.BonePos(rig.foot[1]).y) - 0.08f;
        float surface = -1e9f;
        const Vec3 probes[] = {ch.BonePos(rig.foot[0]), ch.BonePos(rig.foot[1]), ch.BonePos(rig.pelvis), ch.BonePos(rig.SpineTop()), mouth, head};
        for (const Vec3& p : probes) {
            const float d = world.WaterDepth(p);
            if (d > 0.f) { surface = std::max(surface, p.y + d); feel_.waterC = world.WaterTemp(p); }
            footY = std::min(footY, p.y - 0.08f);
        }
        if (surface > -1e8f) {
            const float top = std::max({head.y, mouth.y, footY + 0.2f});
            feel_.inWater = std::clamp((surface - footY) / std::max(0.2f, top - footY), 0.f, 1.f);
            body.WetSkin(feel_.inWater * 250.f * dt);   // ml-equivalent: fully under, she's soaked in a second or two
            feel_.headUnder = world.WaterDepth(mouth) > 0.f;
            if (feel_.headUnder && in.mouth == MouthAction::Open && chew_ <= 0.f) {
                const float ml = 18.f * dt;   // gulping
                body.Drink(ml);
                feel_.drankMl += ml;
            }
        }
    }

    // what reached her this step
    const Intake got = world.TakeIntake();
    const float water = got.ml[static_cast<int>(Liquid::Water)], urine = got.ml[static_cast<int>(Liquid::Urine)];
    body.Drink(water);
    body.Drink(urine, true);
    body.Swallow(got.foodG, got.kcal);
    if (got.skinMl > 0.f) body.WetSkin(got.skinMl - got.skinUrineMl), body.WetSkin(got.skinUrineMl, true);
    feel_.drankMl += water;
    feel_.drankUrineMl = urine;
    feel_.kcal = got.kcal;
    feel_.skinMl = got.skinMl;
}

} // namespace eon
