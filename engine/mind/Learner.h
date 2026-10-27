#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "MemoryGraph.h"
#include "Perception.h"

namespace eon {

// Turns the percept stream into memory (deterministic extraction, no model calls — the Graphify AST pass,
// but for experience): things she sees get her own descriptive names, where they sit, what they feel like
// in the hand, and what her actions seem to make happen. Cause and effect is learned from timing:
// an effect right after one action is an inferred cause; after several actions at once each is an ambiguous
// candidate; doing the action again and nothing happening counts against it.
class Learner {
public:
    MemoryGraph graph;
    double causeWindow = 1.0;       // s: how soon after an action a change counts as its effect
    float moveThreshold = 0.05f;    // m

    void SetBackstory(const std::vector<std::string>& facts, double t);
    void Observe(const Percept& p);
    // What her mind gets this moment: memory relevant to what she sees/holds/feels, within a token budget.
    std::string Context(const Percept& p, int tokenBudget = 300) const;
    std::vector<GraphQuestion> Curiosity(double t, size_t max = 3) const { return graph.Questions(t, max); }
    void Sleep(double t, float hours) { graph.Consolidate(t, hours); }

    uint32_t NodeOf(uint32_t track) const;              // memory node for a percept track (0 = never seen)
    uint32_t Me() const { return me_; }

private:
    struct TrackMem {
        uint32_t node = 0;
        float glow = 0.f;
        bool lit = false;
        Vec3 pos, restPos;
        Vec3 lo, hi;
        bool wide = false;
        double lastSeen = -1.0;
    };
    struct HandMem { uint32_t touching = 0; bool holding = false; uint32_t held = 0; Vec3 grabPos; bool moved = false; bool pressed = false; bool wasPoint = false; };
    struct Recent { uint32_t action; double t; };
    struct Pending { uint32_t action, effect; double deadline; bool met; };

    std::string Name(const SeenThing& s);
    void Act(const std::string& verb, uint32_t thingNode, double t);
    void Happened(const std::string& what, uint32_t thingNode, double t);
    void Property(uint32_t node, const std::string& prop, bool held, double t, Source s = Source::Observed);
    void Spatial(const Percept& p);
    void SelfHappened(const std::string& what, double t);   // an effect on her own body ("I'm less thirsty")
    uint32_t NearMouth(const Percept& p) const;
    double lastDrink_ = -1e9, lastBite_ = -1e9;
    bool wasPeeing_ = false;

    std::unordered_map<uint32_t, TrackMem> tracks_;
    std::unordered_map<std::string, int> nameUse_;
    HandMem hands_[2];
    std::vector<Recent> recent_;
    std::vector<Pending> pending_;
    std::unordered_map<std::string, bool> feeling_;
    uint32_t me_ = 0;
    double lastSpatial_ = -1e9;
};

std::string ColorWord(const Rgb& c);

} // namespace eon
