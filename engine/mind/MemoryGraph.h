#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Math.h"

namespace eon {

// Jenny's memory as a knowledge graph (the Graphify idea, aimed at a mind instead of a codebase):
//  - nodes are things she has met in her own words (never engine names), places, her actions, effects she saw,
//    body feelings, ideas and backstory
//  - every edge carries provenance (observed / inferred / ambiguous / told), evidence counts and recency
//  - retrieval is graph traversal from what she is attending to, ranked and cut to a token budget,
//    instead of re-reading her whole history
//  - updates are incremental (every change is journaled), and sleep consolidates: decay, prune, regroup
enum class NodeKind : uint8_t { Thing, Place, Action, Effect, Feeling, Idea, Self, Count };
enum class Rel : uint8_t {
    On,        // thing rests on thing
    Near,      // often found close together
    In,        // thing is in place
    Has,       // node -> property (Idea node such as "heavy", "movable", "glows")
    Causes,    // action -> effect
    Targets,   // action -> the thing acted on
    Affects,   // effect -> the thing that changed
    Felt,      // effect/action -> feeling
    Knows,     // self -> backstory fact
    Count
};
enum class Source : uint8_t {
    Observed,    // she directly saw/felt it (like Graphify's EXTRACTED)
    Inferred,    // her conclusion from repetition/co-occurrence (INFERRED)
    Ambiguous,   // one of several competing explanations, weighted (AMBIGUOUS)
    Told,        // came with her (backstory), not learned here
    Count
};

const char* KindName(NodeKind k);
const char* RelName(Rel r);
const char* SourceName(Source s);

struct MemNode {
    uint32_t id = 0;
    NodeKind kind = NodeKind::Thing;
    std::string label;            // her words: "small red bump", "press small red bump"
    double firstT = 0, lastT = 0; // game seconds
    uint32_t seen = 0;            // times encountered/attended
    float salience = 0.5f;        // 0..1 how much it matters to her (decays, boosted by surprise/use)
    int community = -1;           // cluster id (e.g. "the table things"); computed
    Vec3 pos;                     // last known place (things)
    bool hasPos = false;
};

struct MemEdge {
    uint32_t from = 0, to = 0;
    Rel rel = Rel::Near;
    Source src = Source::Observed;
    uint32_t support = 0;         // times it held
    uint32_t trials = 0;          // times it was tested (support <= trials)
    float weight = 1.f;           // share when Ambiguous (sums ~1 across competitors)
    double firstT = 0, lastT = 0;
    bool alive = true;

    float Confidence() const { return (support + 1.f) / (trials + 2.f) * weight; }   // Beta(1,1) prior
};

struct GraphQuestion {           // something she doesn't understand yet: a reason to experiment
    std::string text;
    uint32_t node = 0;            // what to go poke
    float priority = 0.f;
};

class MemoryGraph {
public:
    // ---- nodes ----
    uint32_t AddNode(NodeKind k, const std::string& label, double t);
    uint32_t Find(NodeKind k, const std::string& label) const;        // 0 if none
    uint32_t Ensure(NodeKind k, const std::string& label, double t);  // find or add
    MemNode* Node(uint32_t id);
    const MemNode* Node(uint32_t id) const;
    void Touch(uint32_t id, double t, float salienceBoost = 0.f);     // she attended to it
    void Rename(uint32_t id, const std::string& label);

    // ---- edges ----
    // Evidence for/against a relation: held = it was true this time. Creates the edge on first support.
    MemEdge* Evidence(uint32_t from, uint32_t to, Rel r, Source s, bool held, double t, float weight = 1.f);
    MemEdge* Edge(uint32_t from, uint32_t to, Rel r);
    const MemEdge* Edge(uint32_t from, uint32_t to, Rel r) const;
    std::vector<const MemEdge*> Out(uint32_t id, Rel r = Rel::Count) const;   // Count = any
    std::vector<const MemEdge*> In(uint32_t id, Rel r = Rel::Count) const;

    // ---- queries (traversal, not search) ----
    std::vector<uint32_t> Path(uint32_t a, uint32_t b, int maxHops = 6) const;   // undirected BFS
    struct Scored { uint32_t node; float score; int hops; };
    // Spreading activation from what she is attending to: score = salience * confidence * recency / hop decay.
    std::vector<Scored> Focus(const std::vector<uint32_t>& seeds, double now, int maxHops = 2, size_t maxNodes = 40) const;
    // Compact memory text for the mind, cut to ~tokenBudget tokens (4 chars/token), most relevant first.
    std::string Context(const std::vector<uint32_t>& seeds, double now, int tokenBudget = 300) const;
    std::string Describe(uint32_t id, double now) const;   // one line about a node and its strongest links
    std::vector<GraphQuestion> Questions(double now, size_t max = 5) const;
    std::vector<uint32_t> Hubs(size_t n) const;              // most connected ("god nodes")
    std::string Report(double now) const;                    // her journal: what she knows, what puzzles her

    // ---- maintenance ----
    int Communities();                                      // label propagation over strong Near/On edges
    // Sleep: salience decays, weak unsupported guesses are forgotten, ambiguous shares renormalize.
    void Consolidate(double now, float hours);

    // ---- persistence ----
    bool Save(const std::string& path) const;               // full snapshot (JSON)
    bool Load(const std::string& path);                     // snapshot + replays its journal if present
    void SetJournal(const std::string& path) { journalPath_ = path; }   // append every change (incremental)
    bool FlushJournal();
    bool ExportHtml(const std::string& path, double now) const;          // operator view (self-contained)

    size_t NodeCount() const { return nodes_.size(); }
    size_t EdgeCount() const;
    const std::vector<MemNode>& Nodes() const { return nodes_; }
    const std::vector<MemEdge>& Edges() const { return edges_; }
    uint64_t Version() const { return version_; }

private:
    static uint64_t Key(uint32_t a, uint32_t b, Rel r) { return (uint64_t(a) << 34) | (uint64_t(b) << 4) | uint64_t(r); }
    void Log(const std::string& line);
    void Reindex();
    float Recency(double lastT, double now) const;

    std::vector<MemNode> nodes_;   // id = index + 1
    std::vector<MemEdge> edges_;
    std::unordered_map<uint64_t, uint32_t> edgeIndex_;
    std::unordered_map<std::string, uint32_t> labelIndex_;   // kind|label
    std::vector<std::vector<uint32_t>> out_, in_;            // edge indices per node
    std::string journalPath_;
    std::vector<std::string> pending_;
    uint64_t version_ = 0;
};

} // namespace eon
