#pragma once

#include <string>
#include <vector>

#include "Character.h"

namespace eon {

// Mouth shapes (visemes) mapped onto ARKit-style morphs.
enum class Viseme : int { Rest, AA, EE, IH, OH, OO, MBP, FV, TH, LNTD, SZ, CHJ, KG, R, WQ, Count };

struct VisemeKey {
    float time;     // seconds from the start
    Viseme v;
    float weight;   // emphasis 0..1
};

// Turns speech into mouth movement. Two sources:
//  - text: rough letter-to-sound rules build a timed viseme track (works with any TTS, or silent inner speech)
//  - phonemes with timings from a TTS engine (Piper/espeak style), for exact sync with real audio
//  - amplitude: a live loudness value opens the jaw (fallback when only audio is available)
class LipSync {
public:
    void Init(Character& ch);
    void Say(const std::string& text, float wordsPerMinute = 150.f);
    void SayPhonemes(const std::vector<std::pair<std::string, float>>& phonemeDurations);   // ("AA", 0.09)...
    void SetAmplitude(float a) { amplitude_ = a; }
    void Stop();
    bool Speaking() const { return t_ >= 0.f && t_ < length_; }
    void Update(Character& ch, float dt);   // after FaceSim: overrides the mouth while speaking

    const std::vector<VisemeKey>& Track() const { return track_; }

private:
    void Apply(Character& ch, const float* w);
    std::vector<VisemeKey> track_;
    float t_ = -1.f, length_ = 0.f;
    float amplitude_ = 0.f;
    float cur_[static_cast<int>(Viseme::Count)] = {};
    std::vector<std::string> available_;
};

Viseme PhonemeToViseme(const std::string& arpabet);

} // namespace eon
