#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Character.h"

namespace eon {

enum class Layer : int { Underwear = 0, Mid = 1, Outer = 2, Accessory = 3 };

struct Garment {
    std::string name;                        // internal (never shown to Jenny)
    std::string fbx;
    Layer layer = Layer::Mid;
    std::vector<std::string> covers;         // body regions: chest, groin, hips, thighs, feet, arms...
    std::vector<std::string> bodyMorphs;     // body fit morphs to apply while worn (prevents skin poking through)
    std::unordered_map<int, float> support;  // soft tissue tag -> 0..1 (2 breast, 3 glute)
    float clo = 0.05f;                       // insulation
    bool worn = false;
    float off = 0.f;                         // while worn: 0 fully on .. 1 slid all the way off (being taken off / put on)
    Rgb color{230, 175, 185};                // how it looks off her body
    std::vector<int> meshes;                 // in the character asset
};

// What she is wearing. Rules: layers come off outer -> inner and go on inner -> outer (you can't take
// panties off under a skirt you're still wearing... unless it's the skirt itself), and in streamer mode
// the underwear layer stays on.
class Wardrobe {
public:
    std::vector<Garment> garments;
    bool streamerMode = false;

    int Find(const std::string& name) const;
    bool CanRemove(int g, std::string* why = nullptr) const;
    bool CanWear(int g, std::string* why = nullptr) const;
    bool Remove(int g, std::string* why = nullptr);
    bool Wear(int g, std::string* why = nullptr);

    float TotalClo() const;
    float Support(int tag) const;
    bool Covers(const std::string& region) const;   // any worn garment covers it (for censoring)

    // Pushes the state onto the character: fit morphs + soft tissue support. Visibility is the renderer's job.
    void Apply(Character& ch) const;
    std::vector<bool> MeshVisibility(size_t meshCount) const;

private:
    bool Overlaps(const Garment& a, const Garment& b) const;
};

} // namespace eon
