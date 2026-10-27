#pragma once

#include <algorithm>
#include <array>
#include <string>

namespace eon {

// Jenny's physiology (port of the UE EonBody component). Pure simulation: no rendering, no names leak.
enum class Need : int { Fatigue, Hunger, Thirst, Bladder, Hygiene, Boredom, Libido, Count };
enum class Thermal : int { Freezing, Cold, Comfortable, Warm, Hot };

struct BodyInputs {
    float ambientC = 22.f;
    float sunExposure = 0.f;      // 0..1
    float clothingClo = 0.f;      // insulation (nude 0, underwear ~0.1, summer outfit ~0.5)
    float exertion = 0.f;         // 0..1
    float stimulation = 0.f;      // 0..1 novelty/engagement
    bool sleeping = false;
};

class BodyState {
public:
    BodyState();
    void Step(const BodyInputs& in, float realDt);

    float gameMinutesPerSecond = 1.f;

    // needs: 0 satisfied .. 100 critical
    std::array<float, static_cast<int>(Need::Count)> needs{};
    std::array<float, static_cast<int>(Need::Count)> ratePerHour{};
    float mood = 80.f;           // 0..100
    float coreC = 37.f;
    float sweat = 0.f;           // 0..1 sweat rate
    float wetness = 0.f;         // 0..1 skin wetness (for shading)
    float shiver = 0.f;          // 0..1
    float breathRate = 0.25f;    // Hz
    float breathDepth = 0.5f;    // 0..1
    float heartRate = 70.f;      // bpm
    Thermal thermal = Thermal::Comfortable;

    // Sleep: lying still and tired long enough she drifts off; rested, or too uncomfortable, she wakes.
    bool asleep = false;
    float drowsy = 0.f;          // 0..1 builds while lying still
    void UpdateSleep(bool lyingStill, float realDt);

    // Physical intake/output (amounts are real: ml, kcal). Water is absorbed and reaches the bladder over ~45 game min.
    void Drink(float ml, bool urine = false);
    void Eat(float kcal);
    // What she swallows sits in her stomach (the "full" feeling) and empties over ~2 game hours.
    // Its water goes the way drinks do (absorbed, then bladder: she pees it out).
    void Swallow(float grams, float kcal);
    float stomachG = 0.f;             // food in the stomach
    float Fullness() const { return std::clamp(stomachG / 900.f, 0.f, 1.f); }   // 0 empty .. 1 stuffed
    void Urinate(float ml);           // empties the bladder (400 ml = full)
    void WetSkin(float ml, bool urine = false);
    float BladderMl() const { return Get(Need::Bladder) * 4.f; }
    float skinWater = 0.f;            // 0..1 wet from outside (shower, spills); dries over time, evaporation cools
    float absorbing = 0.f;            // ml of drunk water still on its way to the bladder

    float Get(Need n) const { return needs[static_cast<int>(n)]; }
    void Satisfy(Need n, float amount);
    std::string Describe(float ambientC) const;   // first-person-ish body sense for the mind (no world names)

private:
    float exertionSmoothed_ = 0.f;
};

const char* NeedName(Need n);

} // namespace eon
