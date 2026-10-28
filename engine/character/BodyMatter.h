#pragma once

#include "BodyState.h"
#include "Character.h"
#include "Motor.h"
#include "core/World.h"

namespace eon {

// Her body's exchange with physical matter: an open mouth takes in whatever liquid reaches it, biting takes
// bites of food at the mouth, liquid that lands on her wets her skin, and peeing makes a real stream from her
// body that lands wherever it lands (a toilet bowl, the floor, her own legs or underwear).
struct BodyMatterFeel {
    float drankMl = 0.f, drankUrineMl = 0.f, biteG = 0.f, kcal = 0.f;
    float skinMl = 0.f;
    bool peeing = false, soakedClothes = false;
    bool chewing = false;
    float inWater = 0.f;       // 0..1 how much of her is under water
    float waterC = 0.f;        // its temperature
    bool headUnder = false;    // mouth/nose under the surface
};

class BodyMatter {
public:
    // groinCovered: underwear/clothes over the groin (then peeing soaks them first)
    void Step(World& world, const Character& ch, const Motor& motor, const MotorIntent& in, BodyState& body,
              bool groinCovered, float dt);
    const BodyMatterFeel& Feel() const { return feel_; }
    float Jaw() const { return jaw_; }   // 0..1 how far the mouth is open (for the face)
    float peeRate = 14.f;                // ml/s

private:
    BodyMatterFeel feel_;
    float jaw_ = 0.f, chew_ = 0.f, biteCool_ = 0.f;
};

} // namespace eon
