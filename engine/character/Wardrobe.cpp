#include "Wardrobe.h"

#include <algorithm>

namespace eon {

int Wardrobe::Find(const std::string& name) const {
    for (int i = 0; i < (int)garments.size(); ++i) if (garments[i].name == name) return i;
    return -1;
}

bool Wardrobe::Overlaps(const Garment& a, const Garment& b) const {
    for (const std::string& r : a.covers)
        if (std::find(b.covers.begin(), b.covers.end(), r) != b.covers.end()) return true;
    return false;
}

bool Wardrobe::CanRemove(int g, std::string* why) const {
    if (g < 0 || g >= (int)garments.size() || !garments[g].worn) { if (why) *why = "not worn"; return false; }
    const Garment& x = garments[g];
    if (streamerMode && x.layer == Layer::Underwear) { if (why) *why = "streamer mode keeps underwear on"; return false; }
    for (const Garment& o : garments) {
        if (&o == &x || !o.worn || o.layer == Layer::Accessory) continue;
        if (static_cast<int>(o.layer) > static_cast<int>(x.layer) && Overlaps(o, x)) {
            if (why) *why = o.name + " is over it";
            return false;
        }
    }
    return true;
}

bool Wardrobe::CanWear(int g, std::string* why) const {
    if (g < 0 || g >= (int)garments.size() || garments[g].worn) { if (why) *why = "already worn"; return false; }
    const Garment& x = garments[g];
    for (const Garment& o : garments) {
        if (&o == &x || !o.worn || o.layer == Layer::Accessory) continue;
        if (static_cast<int>(o.layer) > static_cast<int>(x.layer) && Overlaps(o, x)) {
            if (why) *why = o.name + " is in the way";
            return false;
        }
        if (o.layer == x.layer && Overlaps(o, x)) {
            if (why) *why = o.name + " is already there";
            return false;
        }
    }
    return true;
}

bool Wardrobe::Remove(int g, std::string* why) {
    if (!CanRemove(g, why)) return false;
    garments[g].worn = false;
    garments[g].off = 0.f;
    return true;
}

bool Wardrobe::Wear(int g, std::string* why) {
    if (!CanWear(g, why)) return false;
    garments[g].worn = true;
    garments[g].off = 0.f;
    return true;
}

float Wardrobe::TotalClo() const {
    float c = 0.f;
    for (const Garment& g : garments) if (g.worn) c += g.clo * (1.f - g.off);
    return c;
}

float Wardrobe::Support(int tag) const {
    float s = 0.f;
    for (const Garment& g : garments) {
        if (!g.worn) continue;
        auto it = g.support.find(tag);
        if (it != g.support.end()) s = std::max(s, it->second * std::max(0.f, 1.f - 2.f * g.off));
    }
    return s;
}

bool Wardrobe::Covers(const std::string& region) const {
    for (const Garment& g : garments)
        if (g.worn && g.off < 0.15f && std::find(g.covers.begin(), g.covers.end(), region) != g.covers.end()) return true;
    return false;
}

void Wardrobe::Apply(Character& ch) const {
    std::unordered_map<std::string, float> morphs;
    for (const Garment& g : garments)
        for (const std::string& m : g.bodyMorphs) morphs[m] = std::max(morphs[m], g.worn && g.off < 0.1f ? 1.f : 0.f);
    for (const auto& [m, w] : morphs) ch.SetMorph(m, w);
    ch.SetSupport(2, Support(2));
    ch.SetSupport(3, Support(3));
}

std::vector<bool> Wardrobe::MeshVisibility(size_t meshCount) const {
    std::vector<bool> v(meshCount, true);
    for (const Garment& g : garments)
        for (int m : g.meshes) if (m >= 0 && m < (int)meshCount) v[m] = g.worn;
    return v;
}

} // namespace eon
