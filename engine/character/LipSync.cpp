#include "LipSync.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace eon {

namespace {

constexpr int kN = static_cast<int>(Viseme::Count);

// morph weights per viseme: jawOpen, mouthFunnel, mouthPucker, mouthClose, mouthPressL/R, mouthStretchL/R,
// mouthSmile, mouthRollLower, mouthUpperUpL/R, mouthLowerDownL/R, tongueOut
struct Shape { float jaw, funnel, pucker, close, press, stretch, smile, rollLower, upperUp, lowerDown, tongue; };

const Shape kShapes[kN] = {
    /*Rest*/ {0.00f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /*AA  */ {0.55f, 0.1f, 0.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 0.2f, 0.4f, 0.0f},
    /*EE  */ {0.22f, 0.0f, 0.0f, 0.0f, 0.0f, 0.45f, 0.35f, 0.0f, 0.15f, 0.2f, 0.0f},
    /*IH  */ {0.28f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f, 0.15f, 0.0f, 0.1f, 0.25f, 0.0f},
    /*OH  */ {0.4f, 0.6f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.15f, 0.0f},
    /*OO  */ {0.15f, 0.35f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /*MBP */ {0.0f, 0.0f, 0.1f, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /*FV  */ {0.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.65f, 0.35f, 0.0f, 0.0f},
    /*TH  */ {0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.1f, 0.15f, 0.35f},
    /*LNTD*/ {0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.15f, 0.05f, 0.0f, 0.1f, 0.15f, 0.08f},
    /*SZ  */ {0.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f, 0.2f, 0.0f, 0.2f, 0.1f, 0.0f},
    /*CHJ */ {0.18f, 0.45f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.1f, 0.0f},
    /*KG  */ {0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 0.05f, 0.2f, 0.0f},
    /*R   */ {0.2f, 0.25f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f},
    /*WQ  */ {0.12f, 0.3f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
};

struct Tok { Viseme v; float dur; float weight; };

// Very small English letter-to-sound pass: digraphs first, then single letters. Good enough for lips.
std::vector<Tok> TextToVisemes(const std::string& textIn, float wpm) {
    std::string t;
    for (char c : textIn) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const float charDur = 60.f / (std::max(60.f, wpm) * 5.5f);   // ~5.5 letters per word incl. spaces
    std::vector<Tok> out;
    auto push = [&](Viseme v, float k, float w = 1.f) { out.push_back({v, charDur * k, w}); };
    auto isV = [](char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'; };
    for (size_t i = 0; i < t.size(); ++i) {
        const char c = t[i];
        const char n = i + 1 < t.size() ? t[i + 1] : ' ';
        auto two = [&](const char* d) { return c == d[0] && n == d[1]; };
        if (c == ' ' ) { push(Viseme::Rest, 0.6f, 0.f); continue; }
        if (c == ',' || c == ';' || c == ':') { push(Viseme::Rest, 3.f, 0.f); continue; }
        if (c == '.' || c == '!' || c == '?') { push(Viseme::Rest, 5.f, 0.f); continue; }
        if (!std::isalpha(static_cast<unsigned char>(c))) continue;
        if (two("th")) { push(Viseme::TH, 1.4f); ++i; continue; }
        if (two("sh") || two("ch")) { push(Viseme::CHJ, 1.5f); ++i; continue; }
        if (two("ph")) { push(Viseme::FV, 1.3f); ++i; continue; }
        if (two("oo") || two("ou")) { push(Viseme::OO, 2.0f); ++i; continue; }
        if (two("ee") || two("ea")) { push(Viseme::EE, 2.0f); ++i; continue; }
        if (two("ng")) { push(Viseme::KG, 1.2f); ++i; continue; }
        if (two("wh")) { push(Viseme::WQ, 1.3f); ++i; continue; }
        switch (c) {
        case 'a': push(Viseme::AA, 1.6f); break;
        case 'e': push(n == ' ' || n == '.' ? Viseme::Rest : Viseme::EE, n == ' ' ? 0.3f : 1.3f, n == ' ' ? 0.f : 1.f); break;
        case 'i': case 'y': push(Viseme::IH, 1.3f); break;
        case 'o': push(Viseme::OH, 1.7f); break;
        case 'u': push(Viseme::OO, 1.4f); break;
        case 'm': case 'b': case 'p': push(Viseme::MBP, 1.2f); break;
        case 'f': case 'v': push(Viseme::FV, 1.2f); break;
        case 'l': case 'n': case 't': case 'd': push(Viseme::LNTD, 1.0f); break;
        case 's': case 'z': case 'c': case 'x': push(Viseme::SZ, isV(n) && c == 'c' ? 1.0f : 1.1f); break;
        case 'j': push(Viseme::CHJ, 1.3f); break;
        case 'k': case 'g': case 'q': push(Viseme::KG, 1.0f); break;
        case 'r': push(Viseme::R, 1.1f); break;
        case 'w': push(Viseme::WQ, 1.2f); break;
        case 'h': push(Viseme::IH, 0.6f, 0.4f); break;
        default: push(Viseme::Rest, 0.8f, 0.f); break;
        }
    }
    push(Viseme::Rest, 3.f, 0.f);
    return out;
}

} // namespace

Viseme PhonemeToViseme(const std::string& p) {
    std::string a;
    for (char c : p) if (std::isalpha(static_cast<unsigned char>(c))) a += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (a == "AA" || a == "AE" || a == "AH" || a == "AY" || a == "AW") return Viseme::AA;
    if (a == "IY" || a == "EY" || a == "EH") return Viseme::EE;
    if (a == "IH" || a == "Y" || a == "HH") return Viseme::IH;
    if (a == "AO" || a == "OW" || a == "OY") return Viseme::OH;
    if (a == "UW" || a == "UH") return Viseme::OO;
    if (a == "M" || a == "B" || a == "P") return Viseme::MBP;
    if (a == "F" || a == "V") return Viseme::FV;
    if (a == "TH" || a == "DH") return Viseme::TH;
    if (a == "L" || a == "N" || a == "T" || a == "D") return Viseme::LNTD;
    if (a == "S" || a == "Z") return Viseme::SZ;
    if (a == "SH" || a == "ZH" || a == "CH" || a == "JH") return Viseme::CHJ;
    if (a == "K" || a == "G" || a == "NG") return Viseme::KG;
    if (a == "R" || a == "ER") return Viseme::R;
    if (a == "W") return Viseme::WQ;
    return Viseme::Rest;
}

void LipSync::Init(Character& ch) { available_ = ch.MorphNames(); }

void LipSync::Say(const std::string& text, float wpm) {
    track_.clear();
    float t = 0.f;
    for (const Tok& k : TextToVisemes(text, wpm)) {
        track_.push_back({t, k.v, k.weight});
        t += k.dur;
    }
    length_ = t;
    t_ = 0.f;
}

void LipSync::SayPhonemes(const std::vector<std::pair<std::string, float>>& ph) {
    track_.clear();
    float t = 0.f;
    for (const auto& [p, d] : ph) {
        const Viseme v = PhonemeToViseme(p);
        track_.push_back({t, v, v == Viseme::Rest ? 0.f : 1.f});
        t += d;
    }
    track_.push_back({t, Viseme::Rest, 0.f});
    length_ = t + 0.15f;
    t_ = 0.f;
}

void LipSync::Stop() { t_ = -1.f; }

void LipSync::Apply(Character& ch, const float* w) {
    Shape s{};
    for (int v = 0; v < kN; ++v) {
        const Shape& k = kShapes[v];
        s.jaw += k.jaw * w[v]; s.funnel += k.funnel * w[v]; s.pucker += k.pucker * w[v]; s.close += k.close * w[v];
        s.press += k.press * w[v]; s.stretch += k.stretch * w[v]; s.smile += k.smile * w[v]; s.rollLower += k.rollLower * w[v];
        s.upperUp += k.upperUp * w[v]; s.lowerDown += k.lowerDown * w[v]; s.tongue += k.tongue * w[v];
    }
    s.jaw = std::max(s.jaw, amplitude_ * 0.6f);
    auto set = [&](const char* n, float x) {
        if (std::find(available_.begin(), available_.end(), n) != available_.end()) ch.SetMorph(n, std::clamp(x, 0.f, 1.f));
    };
    // blend over whatever expression the face already has (a smile stays while she talks)
    set("jawOpen", std::max(ch.GetMorph("jawOpen") * 0.3f, s.jaw));
    set("mouthFunnel", s.funnel);
    set("mouthPucker", s.pucker);
    set("mouthClose", s.close);
    set("mouthPressLeft", std::max(ch.GetMorph("mouthPressLeft") * 0.5f, s.press));
    set("mouthPressRight", std::max(ch.GetMorph("mouthPressRight") * 0.5f, s.press));
    set("mouthStretchLeft", s.stretch);
    set("mouthStretchRight", s.stretch);
    set("mouthSmile", std::max(ch.GetMorph("mouthSmile"), s.smile));
    set("mouthRollLower", s.rollLower);
    set("mouthUpperUpLeft", s.upperUp);
    set("mouthUpperUpRight", s.upperUp);
    set("mouthLowerDownLeft", s.lowerDown);
    set("mouthLowerDownRight", s.lowerDown);
    set("tongueOut", s.tongue * 0.3f);
}

void LipSync::Update(Character& ch, float dt) {
    float target[kN] = {};
    if (Speaking()) {
        t_ += dt;
        // current and next key; anticipate the next mouth shape slightly (coarticulation)
        size_t k = 0;
        while (k + 1 < track_.size() && track_[k + 1].time <= t_) ++k;
        const VisemeKey& a = track_[k];
        target[static_cast<int>(a.v)] += a.weight;
        if (k + 1 < track_.size()) {
            const VisemeKey& b = track_[k + 1];
            const float span = std::max(1e-3f, b.time - a.time);
            const float u = std::clamp((t_ - a.time) / span, 0.f, 1.f);
            target[static_cast<int>(b.v)] += b.weight * std::max(0.f, (u - 0.5f) * 2.f) * 0.6f;
        }
    } else if (amplitude_ <= 0.01f) {
        bool any = false;
        for (float c : cur_) any |= c > 0.01f;
        if (!any) return;   // idle: leave the face alone
    }
    // lips move fast but not instantly (~60 ms)
    const float k = std::clamp(dt * 18.f, 0.f, 1.f);
    for (int v = 0; v < kN; ++v) cur_[v] += (target[v] - cur_[v]) * k;
    Apply(ch, cur_);
}

} // namespace eon
