#include "BodyState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace eon {

namespace {
constexpr int N(Need n) { return static_cast<int>(n); }
}

const char* NeedName(Need n) {
    switch (n) {
    case Need::Fatigue: return "tired";
    case Need::Hunger: return "hungry";
    case Need::Thirst: return "thirsty";
    case Need::Bladder: return "need to pee";
    case Need::Hygiene: return "feel grimy";
    case Need::Boredom: return "bored";
    case Need::Libido: return "aroused";
    default: return "?";
    }
}

BodyState::BodyState() {
    const float start[] = {20, 30, 25, 15, 10, 20, 15};
    const float rate[] = {6, 6, 8, 10, 2, 10, 3};
    for (int i = 0; i < N(Need::Count); ++i) { needs[i] = start[i]; ratePerHour[i] = rate[i]; }
}

void BodyState::Satisfy(Need n, float amount) {
    float& v = needs[N(n)];
    v = std::clamp(v - amount, 0.f, 100.f);
    if (n == Need::Thirst) needs[N(Need::Bladder)] = std::min(100.f, needs[N(Need::Bladder)] + amount * 0.3f);
    if (n == Need::Hunger) needs[N(Need::Bladder)] = std::min(100.f, needs[N(Need::Bladder)] + amount * 0.05f);
}

void BodyState::Step(const BodyInputs& in, float realDt) {
    const float hours = realDt * gameMinutesPerSecond / 60.f;
    exertionSmoothed_ += (in.exertion - exertionSmoothed_) * std::clamp(realDt * 0.5f, 0.f, 1.f);

    // ---- Thermal (same model as the UE component) ----
    sweat = std::clamp((coreC - 37.2f) / 0.8f, 0.f, 1.f);
    shiver = std::clamp((36.6f - coreC) / 1.0f, 0.f, 1.f);
    const float effective = in.ambientC + in.clothingClo * 7.f + in.sunExposure * 8.f + exertionSmoothed_ * 12.f;
    float target = 37.f + (effective - 28.f) * 0.06f - sweat * 0.8f + shiver * 0.5f;
    target = std::clamp(target, 33.f, 41.f);
    coreC += (target - coreC) * (1.f - std::exp(-2.f * hours));
    wetness += (sweat - wetness) * (1.f - std::exp(-4.f * hours));

    if (coreC < 35.5f) thermal = Thermal::Freezing;
    else if (coreC < 36.5f) thermal = Thermal::Cold;
    else if (coreC < 37.3f) thermal = Thermal::Comfortable;
    else if (coreC < 38.f) thermal = Thermal::Warm;
    else thermal = Thermal::Hot;

    // ---- Needs ----
    auto add = [&](Need n, float d) { float& v = needs[N(n)]; v = std::clamp(v + d, 0.f, 100.f); };
    const float sleepK = in.sleeping ? 0.5f : 1.f;
    if (in.sleeping) add(Need::Fatigue, -14.f * hours);
    else add(Need::Fatigue, ratePerHour[N(Need::Fatigue)] * (1.f + exertionSmoothed_ + shiver * 0.5f) * hours);
    add(Need::Hunger, ratePerHour[N(Need::Hunger)] * sleepK * (1.f + exertionSmoothed_ * 0.5f + shiver * 0.5f) * hours);
    add(Need::Thirst, ratePerHour[N(Need::Thirst)] * sleepK * (1.f + sweat * 2.f) * hours);
    add(Need::Bladder, ratePerHour[N(Need::Bladder)] * sleepK * hours);
    add(Need::Hygiene, ratePerHour[N(Need::Hygiene)] * (1.f + sweat * 3.f) * hours);
    if (!in.sleeping) add(Need::Boredom, (ratePerHour[N(Need::Boredom)] - in.stimulation * 40.f) * hours);
    const bool drained = Get(Need::Fatigue) > 75 || Get(Need::Hunger) > 75 || thermal == Thermal::Freezing || thermal == Thermal::Hot;
    add(Need::Libido, ratePerHour[N(Need::Libido)] * sleepK * (drained ? 0.25f : 1.f) * hours);

    // ---- Mood ----
    float penalty = 0.f;
    for (int i = 0; i < N(Need::Count); ++i) {
        const float over = std::max(0.f, needs[i] - 40.f);
        const float wgt = i == N(Need::Libido) ? 0.15f : (i == N(Need::Hygiene) ? 0.2f : 0.35f);
        penalty += over * wgt;
    }
    penalty += std::fabs(coreC - 37.f) * 15.f;
    mood = std::clamp(100.f - penalty, 0.f, 100.f);

    // ---- Breathing / heart (drive the visible body) ----
    const float arousal = std::clamp((Get(Need::Libido) - 60.f) / 40.f, 0.f, 1.f);
    const float targetRate = in.sleeping ? 0.2f : 0.25f + 0.45f * exertionSmoothed_ + 0.1f * sweat + 0.1f * arousal + 0.08f * shiver;
    breathRate += (targetRate - breathRate) * std::clamp(realDt * 0.5f, 0.f, 1.f);
    const float targetDepth = in.sleeping ? 0.7f : 0.4f + 0.6f * exertionSmoothed_;
    breathDepth += (targetDepth - breathDepth) * std::clamp(realDt * 0.5f, 0.f, 1.f);
    const float targetHr = (in.sleeping ? 58.f : 70.f) + 90.f * exertionSmoothed_ + 15.f * arousal + 10.f * sweat;
    heartRate += (targetHr - heartRate) * std::clamp(realDt * 0.3f, 0.f, 1.f);
}

void BodyState::UpdateSleep(bool lyingStill, float realDt) {
    const float minutes = realDt * gameMinutesPerSecond;
    const float fatigue = Get(Need::Fatigue);
    const bool uncomfortable = Get(Need::Bladder) > 90.f || Get(Need::Hunger) > 95.f || Get(Need::Thirst) > 95.f ||
                               thermal == Thermal::Freezing || thermal == Thermal::Hot;
    if (asleep) {
        if (!lyingStill || uncomfortable || fatigue < 3.f) { asleep = false; drowsy = 0.f; }
        return;
    }
    if (lyingStill && fatigue > 25.f && !uncomfortable) {
        // very tired: a few minutes; mildly tired: half an hour
        const float onsetMin = 30.f - 27.f * std::clamp((fatigue - 25.f) / 60.f, 0.f, 1.f);
        drowsy += minutes / onsetMin;
        if (drowsy >= 1.f) asleep = true;
    } else {
        drowsy = std::max(0.f, drowsy - minutes / 5.f);
    }
}

std::string BodyState::Describe(float ambientC) const {
    if (asleep) return "(asleep)";
    const char* feel[] = {"freezing", "cold", "comfortable", "warm", "hot"};
    std::string s = std::string("I feel ") + feel[static_cast<int>(thermal)];
    if (drowsy > 0.5f) s += ", drowsy";
    if (sweat > 0.05f) s += sweat > 0.6f ? ", sweating a lot" : ", a little sweaty";
    if (shiver > 0.05f) s += ", shivering";
    std::string needsTxt;
    for (int i = 0; i < N(Need::Count); ++i) {
        if (needs[i] < 50.f) continue;
        if (!needsTxt.empty()) needsTxt += ", ";
        needsTxt += (needs[i] >= 75.f ? "really " : "a bit ");
        needsTxt += NeedName(static_cast<Need>(i));
    }
    if (!needsTxt.empty()) s += ". I'm " + needsTxt;
    char buf[64];
    std::snprintf(buf, sizeof(buf), ". Mood %d/100.", static_cast<int>(mood));
    (void)ambientC;   // she feels temperature, she doesn't read a thermometer
    return s + buf;
}

} // namespace eon
