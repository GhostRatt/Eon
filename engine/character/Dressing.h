#pragma once
// Getting clothes off and on with her own hands. No "undress" command: a garment comes off when she grips it
// where a person would (the waistband at her hips, the band of a bra) and moves her hands along the way it
// slides off (down the legs to the ankles, away from the chest). Fully off, it becomes a real object in her
// hand (light, soft, she can drop or carry it). To dress, she holds that object at the far end of the path
// (her feet, her chest) and draws it back along the path; let go part way and it stays there (around her knees).
#include <string>
#include <utility>
#include <vector>

#include "Character.h"
#include "Motor.h"
#include "Wardrobe.h"
#include "core/World.h"

namespace eon {

enum class GarmentKind { Bottom, Top, Feet };
GarmentKind KindOf(const Garment& g);

class Dressing {
public:
    // Call after the motor's PostPhysics (hands are where they are this frame) and before Skin().
    void Step(World& world, Character& ch, Motor& motor, const MotorIntent& in, Wardrobe& wardrobe, float dt);

    // A garment that starts the scene off her body, lying somewhere (as an object).
    void PlaceOff(World& world, Wardrobe& wardrobe, int garment, const Vec3& pos);

    float Off(const Wardrobe& w, int g) const { return w.garments[g].worn ? w.garments[g].off : 1.f; }
    int ObjectOf(int g) const { return g >= 0 && g < (int)st_.size() ? st_[g].entity : 0; }   // its world object while off
    bool Holding(int g) const { return g >= 0 && g < (int)st_.size() && (st_[g].hand[0] || st_[g].hand[1]); }
    // What she feels this frame (snug on her hips, bunched around her knees, bare...), for Perception.
    const std::vector<std::pair<std::string, float>>& Feelings() const { return feel_; }

    float grabReach = 0.09f;   // palm must be this close to where the garment can be taken hold of
    float takeReach = 0.14f;   // holding it, this close to her feet/chest to start putting it on

private:
    struct St { int entity = 0; bool hand[2] = {false, false}; bool atFeet = false; Vec3 feetAt[2]; bool justOff = false; };
    void Path(const Character& ch, GarmentKind k, int side, std::vector<Vec3>& out) const;
    float Along(const std::vector<Vec3>& path, const Vec3& p, float* dist = nullptr) const;   // 0..1 nearest point
    Vec3 At(const std::vector<Vec3>& path, float t) const;
    int SpawnObject(World& world, const Garment& g, const Vec3& pos);
    std::vector<St> st_;
    bool wasGrip_[2] = {false, false};
    std::vector<std::pair<std::string, float>> feel_;
};

} // namespace eon
