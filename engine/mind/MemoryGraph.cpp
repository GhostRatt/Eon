#include "MemoryGraph.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <sstream>

#include <nlohmann/json.hpp>

namespace eon {

using json = nlohmann::json;

const char* KindName(NodeKind k) {
    static const char* n[] = {"thing", "place", "action", "effect", "feeling", "idea", "self"};
    return k < NodeKind::Count ? n[int(k)] : "?";
}
const char* RelName(Rel r) {
    static const char* n[] = {"on", "near", "in", "has", "causes", "targets", "affects", "felt", "knows"};
    return r < Rel::Count ? n[int(r)] : "?";
}
const char* SourceName(Source s) {
    static const char* n[] = {"observed", "inferred", "ambiguous", "told"};
    return s < Source::Count ? n[int(s)] : "?";
}

namespace {
std::string LabelKey(NodeKind k, const std::string& l) { return std::to_string(int(k)) + "|" + l; }
// how sure she sounds about a link
const char* Hedge(const MemEdge& e) {
    if (e.src == Source::Told) return "";
    if (e.src == Source::Ambiguous) return "maybe ";
    const float c = e.Confidence();
    if (e.src == Source::Inferred) return c > 0.75f ? "" : c > 0.5f ? "probably " : "maybe ";
    return "";
}
}

// ---------------- nodes ----------------

uint32_t MemoryGraph::AddNode(NodeKind k, const std::string& label, double t) {
    MemNode n;
    n.id = static_cast<uint32_t>(nodes_.size()) + 1;
    n.kind = k;
    n.label = label;
    n.firstT = n.lastT = t;
    n.seen = 1;
    nodes_.push_back(n);
    out_.emplace_back();
    in_.emplace_back();
    labelIndex_[LabelKey(k, label)] = n.id;
    ++version_;
    Log(json{{"op", "n"}, {"k", int(k)}, {"l", label}, {"t", t}}.dump());
    return n.id;
}

uint32_t MemoryGraph::Find(NodeKind k, const std::string& label) const {
    auto it = labelIndex_.find(LabelKey(k, label));
    return it == labelIndex_.end() ? 0 : it->second;
}

uint32_t MemoryGraph::Ensure(NodeKind k, const std::string& label, double t) {
    const uint32_t id = Find(k, label);
    return id ? id : AddNode(k, label, t);
}

MemNode* MemoryGraph::Node(uint32_t id) { return id >= 1 && id <= nodes_.size() ? &nodes_[id - 1] : nullptr; }
const MemNode* MemoryGraph::Node(uint32_t id) const { return id >= 1 && id <= nodes_.size() ? &nodes_[id - 1] : nullptr; }

void MemoryGraph::Touch(uint32_t id, double t, float boost) {
    MemNode* n = Node(id);
    if (!n) return;
    // count encounters, not frames: a new encounter after a few seconds away
    if (t - n->lastT > 3.0) ++n->seen;
    n->lastT = t;
    n->salience = std::clamp(n->salience + boost, 0.f, 1.f);
    if (boost > 0.f) Log(json{{"op", "s"}, {"i", id}, {"b", boost}, {"t", t}}.dump());
}

void MemoryGraph::Rename(uint32_t id, const std::string& label) {
    MemNode* n = Node(id);
    if (!n || n->label == label) return;
    labelIndex_.erase(LabelKey(n->kind, n->label));
    n->label = label;
    labelIndex_[LabelKey(n->kind, label)] = id;
    ++version_;
    Log(json{{"op", "r"}, {"i", id}, {"l", label}}.dump());
}

// ---------------- edges ----------------

MemEdge* MemoryGraph::Evidence(uint32_t from, uint32_t to, Rel r, Source s, bool held, double t, float weight) {
    if (!Node(from) || !Node(to) || from == to) return nullptr;
    const uint64_t k = Key(from, to, r);
    auto it = edgeIndex_.find(k);
    MemEdge* e = nullptr;
    if (it == edgeIndex_.end()) {
        if (!held) return nullptr;   // no edge for something never seen to hold
        MemEdge ne;
        ne.from = from; ne.to = to; ne.rel = r; ne.src = s; ne.weight = weight;
        ne.firstT = t;
        edges_.push_back(ne);
        const uint32_t idx = static_cast<uint32_t>(edges_.size()) - 1;
        edgeIndex_[k] = idx;
        out_[from - 1].push_back(idx);
        in_[to - 1].push_back(idx);
        e = &edges_[idx];
    } else {
        e = &edges_[it->second];
    }
    e->alive = true;
    ++e->trials;
    if (held) { ++e->support; e->lastT = t; }
    // provenance only upgrades with evidence: a guess seen to hold directly becomes observed;
    // a competing explanation that keeps holding alone stops being ambiguous
    if (s == Source::Observed && held) e->src = Source::Observed;
    else if (e->src == Source::Ambiguous && s == Source::Inferred) e->src = Source::Inferred;
    if (held && (s != Source::Ambiguous || e->src == Source::Ambiguous)) e->weight = weight;
    ++version_;
    Log(json{{"op", "e"}, {"a", from}, {"b", to}, {"r", int(r)}, {"s", int(s)}, {"h", held}, {"t", t}, {"w", weight}}.dump());
    return e;
}

MemEdge* MemoryGraph::Edge(uint32_t a, uint32_t b, Rel r) {
    auto it = edgeIndex_.find(Key(a, b, r));
    return it == edgeIndex_.end() || !edges_[it->second].alive ? nullptr : &edges_[it->second];
}
const MemEdge* MemoryGraph::Edge(uint32_t a, uint32_t b, Rel r) const {
    auto it = edgeIndex_.find(Key(a, b, r));
    return it == edgeIndex_.end() || !edges_[it->second].alive ? nullptr : &edges_[it->second];
}

std::vector<const MemEdge*> MemoryGraph::Out(uint32_t id, Rel r) const {
    std::vector<const MemEdge*> v;
    if (!Node(id)) return v;
    for (uint32_t i : out_[id - 1]) if (edges_[i].alive && (r == Rel::Count || edges_[i].rel == r)) v.push_back(&edges_[i]);
    return v;
}
std::vector<const MemEdge*> MemoryGraph::In(uint32_t id, Rel r) const {
    std::vector<const MemEdge*> v;
    if (!Node(id)) return v;
    for (uint32_t i : in_[id - 1]) if (edges_[i].alive && (r == Rel::Count || edges_[i].rel == r)) v.push_back(&edges_[i]);
    return v;
}

size_t MemoryGraph::EdgeCount() const {
    size_t n = 0;
    for (const MemEdge& e : edges_) n += e.alive;
    return n;
}

// ---------------- queries ----------------

std::vector<uint32_t> MemoryGraph::Path(uint32_t a, uint32_t b, int maxHops) const {
    if (!Node(a) || !Node(b)) return {};
    std::vector<uint32_t> prev(nodes_.size() + 1, 0);
    std::vector<int> depth(nodes_.size() + 1, -1);
    std::queue<uint32_t> q;
    q.push(a);
    depth[a] = 0;
    while (!q.empty()) {
        const uint32_t u = q.front();
        q.pop();
        if (u == b) break;
        if (depth[u] >= maxHops) continue;
        auto visit = [&](uint32_t v) { if (depth[v] < 0) { depth[v] = depth[u] + 1; prev[v] = u; q.push(v); } };
        for (uint32_t i : out_[u - 1]) if (edges_[i].alive) visit(edges_[i].to);
        for (uint32_t i : in_[u - 1]) if (edges_[i].alive) visit(edges_[i].from);
    }
    if (depth[b] < 0) return {};
    std::vector<uint32_t> p;
    for (uint32_t v = b; v != a; v = prev[v]) p.push_back(v);
    p.push_back(a);
    std::reverse(p.begin(), p.end());
    return p;
}

float MemoryGraph::Recency(double lastT, double now) const {
    const double age = std::max(0.0, now - lastT);
    return 0.25f + 0.75f * static_cast<float>(std::exp(-age / 1800.0));   // fades over ~half an hour, never to zero
}

std::vector<MemoryGraph::Scored> MemoryGraph::Focus(const std::vector<uint32_t>& seeds, double now, int maxHops, size_t maxNodes) const {
    std::unordered_map<uint32_t, Scored> best;
    std::vector<std::pair<uint32_t, float>> frontier;
    for (uint32_t s : seeds) {
        const MemNode* n = Node(s);
        if (!n) continue;
        const float sc = 0.5f + 0.5f * n->salience;
        if (best.count(s) && best[s].score >= sc) continue;
        best[s] = {s, sc, 0};
        frontier.push_back({s, sc});
    }
    for (int hop = 1; hop <= maxHops; ++hop) {
        std::vector<std::pair<uint32_t, float>> next;
        for (const auto& [u, su] : frontier) {
            auto spread = [&](const MemEdge& e, uint32_t v) {
                const MemNode* n = Node(v);
                const float sc = su * 0.6f * e.Confidence() * Recency(e.lastT, now) * (0.5f + 0.5f * n->salience);
                auto it = best.find(v);
                if (it != best.end() && it->second.score >= sc) return;
                best[v] = {v, sc, hop};
                next.push_back({v, sc});
            };
            for (uint32_t i : out_[u - 1]) if (edges_[i].alive) spread(edges_[i], edges_[i].to);
            for (uint32_t i : in_[u - 1]) if (edges_[i].alive) spread(edges_[i], edges_[i].from);
        }
        frontier.swap(next);
    }
    std::vector<Scored> v;
    for (auto& [id, s] : best) v.push_back(s);
    std::sort(v.begin(), v.end(), [](const Scored& a, const Scored& b) { return a.score != b.score ? a.score > b.score : a.node < b.node; });
    if (v.size() > maxNodes) v.resize(maxNodes);
    return v;
}

std::string MemoryGraph::Describe(uint32_t id, double now) const {
    const MemNode* n = Node(id);
    if (!n) return {};
    std::ostringstream o;
    o << n->label;
    std::vector<std::string> bits;
    auto strong = [&](std::vector<const MemEdge*> v) {
        std::sort(v.begin(), v.end(), [&](const MemEdge* a, const MemEdge* b) { return a->Confidence() * Recency(a->lastT, now) > b->Confidence() * Recency(b->lastT, now); });
        return v;
    };
    for (const MemEdge* e : strong(Out(id, Rel::Has))) if (e->Confidence() > 0.4f) bits.push_back(std::string(Hedge(*e)) + Node(e->to)->label);
    for (const MemEdge* e : strong(Out(id, Rel::On))) { bits.push_back(std::string(Hedge(*e)) + "on " + Node(e->to)->label); break; }
    for (const MemEdge* e : strong(Out(id, Rel::In))) { bits.push_back("in " + Node(e->to)->label); break; }
    if (n->kind == NodeKind::Thing) {
        // what she has done to it and what happened
        for (const MemEdge* t : In(id, Rel::Targets)) {
            const MemNode* act = Node(t->from);
            for (const MemEdge* c : strong(Out(t->from, Rel::Causes))) {
                if (c->Confidence() < 0.2f) continue;
                std::ostringstream b;
                b << Hedge(*c) << act->label << " -> " << Node(c->to)->label << " (" << c->support << "/" << c->trials << ")";
                bits.push_back(b.str());
            }
            if (Out(t->from, Rel::Causes).empty() && act->seen > 0) bits.push_back(act->label + " -> nothing yet");
        }
    }
    if (n->kind == NodeKind::Effect) {
        for (const MemEdge* c : strong(In(id, Rel::Causes))) {
            std::ostringstream b;
            b << Hedge(*c) << "after " << Node(c->from)->label << " (" << c->support << "/" << c->trials << ")";
            bits.push_back(b.str());
        }
    }
    if (n->kind == NodeKind::Self || n->kind == NodeKind::Idea)
        for (const MemEdge* e : Out(id, Rel::Knows)) bits.push_back(Node(e->to)->label);
    if (!bits.empty()) {
        o << ": ";
        for (size_t i = 0; i < bits.size() && i < 6; ++i) o << (i ? "; " : "") << bits[i];
    }
    return o.str();
}

std::string MemoryGraph::Context(const std::vector<uint32_t>& seeds, double now, int tokenBudget) const {
    const size_t budget = static_cast<size_t>(std::max(16, tokenBudget)) * 4;   // ~4 chars per token
    std::string out = "I remember:\n";
    for (const Scored& s : Focus(seeds, now, 2, 64)) {
        const MemNode* n = Node(s.node);
        if (n->kind == NodeKind::Action || n->kind == NodeKind::Idea) continue;   // shown through the things they're about
        const std::string line = "- " + Describe(s.node, now) + "\n";
        if (out.size() + line.size() > budget) break;
        out += line;
    }
    const auto qs = Questions(now, 3);
    for (const GraphQuestion& q : qs) {
        const std::string line = "? " + q.text + "\n";
        if (out.size() + line.size() > budget) break;
        out += line;
    }
    return out;
}

std::vector<GraphQuestion> MemoryGraph::Questions(double now, size_t max) const {
    std::vector<GraphQuestion> qs;
    for (const MemNode& n : nodes_) {
        if (n.kind == NodeKind::Effect) {
            const auto causes = In(n.id, Rel::Causes);
            int ambiguous = 0, solid = 0;
            for (const MemEdge* c : causes) {
                if (c->src == Source::Ambiguous) ++ambiguous;
                else if (c->Confidence() > 0.6f) ++solid;
            }
            const auto aff = Out(n.id, Rel::Affects);
            const uint32_t thing = aff.empty() ? n.id : aff[0]->to;
            if (causes.empty()) qs.push_back({"What caused this: " + n.label + "?", thing, 0.9f * (0.5f + n.salience)});
            else if (ambiguous > 1 && solid == 0) qs.push_back({"Which of the things I did caused this: " + n.label + "?", thing, 0.8f * (0.5f + n.salience)});
            for (const MemEdge* c : causes)
                if (c->src != Source::Ambiguous && c->trials < 3)
                    qs.push_back({"Does it happen every time I " + Node(c->from)->label + "? (" + n.label + ")", thing, 0.5f * (0.5f + n.salience)});
        } else if (n.kind == NodeKind::Thing && n.seen >= 2) {
            if (In(n.id, Rel::Targets).empty() && Out(n.id, Rel::Has).empty())
                qs.push_back({"What is " + n.label + "? I haven't touched it.", n.id, 0.35f * (0.5f + n.salience) * Recency(n.lastT, now)});
        }
    }
    std::sort(qs.begin(), qs.end(), [](const GraphQuestion& a, const GraphQuestion& b) { return a.priority != b.priority ? a.priority > b.priority : a.node < b.node; });
    if (qs.size() > max) qs.resize(max);
    return qs;
}

std::vector<uint32_t> MemoryGraph::Hubs(size_t n) const {
    std::vector<std::pair<size_t, uint32_t>> deg;
    for (const MemNode& m : nodes_) {
        if (m.kind != NodeKind::Thing) continue;
        size_t d = 0;
        for (uint32_t i : out_[m.id - 1]) d += edges_[i].alive;
        for (uint32_t i : in_[m.id - 1]) d += edges_[i].alive;
        deg.push_back({d, m.id});
    }
    std::sort(deg.begin(), deg.end(), [](auto& a, auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    std::vector<uint32_t> v;
    for (size_t i = 0; i < deg.size() && i < n; ++i) v.push_back(deg[i].second);
    return v;
}

std::string MemoryGraph::Report(double now) const {
    std::ostringstream o;
    o << "# What I know\n\n";
    o << nodes_.size() << " memories, " << EdgeCount() << " connections.\n\n";
    o << "## Things that matter most\n";
    for (uint32_t id : Hubs(8)) o << "- " << Describe(id, now) << "\n";
    o << "\n## What makes things happen\n";
    for (const MemEdge& e : edges_) {
        if (!e.alive || e.rel != Rel::Causes) continue;
        o << "- " << Hedge(e) << Node(e.from)->label << " -> " << Node(e.to)->label << " (" << e.support << " of " << e.trials
          << " times, " << SourceName(e.src) << ")\n";
    }
    std::unordered_map<int, std::vector<uint32_t>> groups;
    for (const MemNode& n : nodes_) if (n.kind == NodeKind::Thing && n.community >= 0) groups[n.community].push_back(n.id);
    if (!groups.empty()) {
        o << "\n## Things that belong together\n";
        std::vector<int> keys;
        for (auto& [k, v] : groups) if (v.size() > 1) keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        for (int k : keys) {
            o << "-";
            for (size_t i = 0; i < groups[k].size() && i < 8; ++i) o << (i ? ", " : " ") << Node(groups[k][i])->label;
            o << "\n";
        }
    }
    o << "\n## What puzzles me\n";
    for (const GraphQuestion& q : Questions(now, 8)) o << "- " << q.text << "\n";
    return o.str();
}

// ---------------- maintenance ----------------

int MemoryGraph::Communities() {
    // label propagation on things linked by strong on/near edges (deterministic: fixed order, ties -> smaller label)
    std::vector<int> lab(nodes_.size() + 1, -1);
    for (const MemNode& n : nodes_) if (n.kind == NodeKind::Thing) lab[n.id] = int(n.id);
    for (int it = 0; it < 12; ++it) {
        bool changed = false;
        for (const MemNode& n : nodes_) {
            if (n.kind != NodeKind::Thing) continue;
            std::unordered_map<int, float> votes;
            auto vote = [&](const MemEdge& e, uint32_t v) {
                if ((e.rel == Rel::On || e.rel == Rel::Near) && lab[v] >= 0) votes[lab[v]] += e.Confidence();
            };
            for (uint32_t i : out_[n.id - 1]) if (edges_[i].alive) vote(edges_[i], edges_[i].to);
            for (uint32_t i : in_[n.id - 1]) if (edges_[i].alive) vote(edges_[i], edges_[i].from);
            int bestL = lab[n.id];
            float bestV = votes.count(bestL) ? votes[bestL] : 0.f;
            for (auto& [l, v] : votes) if (v > bestV + 1e-6f || (std::fabs(v - bestV) < 1e-6f && l < bestL)) { bestL = l; bestV = v; }
            if (bestL != lab[n.id]) { lab[n.id] = bestL; changed = true; }
        }
        if (!changed) break;
    }
    std::unordered_map<int, int> remap;
    for (MemNode& n : nodes_) {
        if (lab[n.id] < 0) { n.community = -1; continue; }
        auto it = remap.find(lab[n.id]);
        if (it == remap.end()) it = remap.emplace(lab[n.id], int(remap.size())).first;
        n.community = it->second;
    }
    return static_cast<int>(remap.size());
}

void MemoryGraph::Consolidate(double now, float hours) {
    const float decay = std::exp(-0.08f * hours);
    for (MemNode& n : nodes_) n.salience = std::max(0.05f, n.salience * decay);
    // forget guesses that keep failing, and one-off "near" impressions that never came back
    for (MemEdge& e : edges_) {
        if (!e.alive) continue;
        const bool guess = e.src == Source::Inferred || e.src == Source::Ambiguous;
        if (guess && e.trials >= 3 && float(e.support) / float(e.trials) < 0.34f) e.alive = false;
        if (e.rel == Rel::Near && e.support <= 1 && now - e.lastT > 3600.0) e.alive = false;
    }
    // competing explanations for the same effect: share by how often each held
    for (const MemNode& n : nodes_) {
        if (n.kind != NodeKind::Effect) continue;
        std::vector<MemEdge*> amb;
        float sum = 0.f;
        for (uint32_t i : in_[n.id - 1]) {
            MemEdge& e = edges_[i];
            if (e.alive && e.rel == Rel::Causes && e.src == Source::Ambiguous) { amb.push_back(&e); sum += float(e.support); }
        }
        if (amb.size() < 2 || sum <= 0.f) continue;
        for (MemEdge* e : amb) e->weight = std::max(0.05f, float(e->support) / sum);
    }
    Communities();
    ++version_;
    Log(json{{"op", "c"}, {"t", now}, {"h", hours}}.dump());
}

// ---------------- persistence ----------------

void MemoryGraph::Log(const std::string& line) {
    if (!journalPath_.empty()) pending_.push_back(line);
}

bool MemoryGraph::FlushJournal() {
    if (journalPath_.empty() || pending_.empty()) return true;
    std::ofstream f(journalPath_, std::ios::app);
    if (!f) return false;
    for (const std::string& l : pending_) f << l << "\n";
    pending_.clear();
    return true;
}

void MemoryGraph::Reindex() {
    edgeIndex_.clear();
    labelIndex_.clear();
    out_.assign(nodes_.size(), {});
    in_.assign(nodes_.size(), {});
    for (const MemNode& n : nodes_) labelIndex_[LabelKey(n.kind, n.label)] = n.id;
    for (uint32_t i = 0; i < edges_.size(); ++i) {
        const MemEdge& e = edges_[i];
        edgeIndex_[Key(e.from, e.to, e.rel)] = i;
        out_[e.from - 1].push_back(i);
        in_[e.to - 1].push_back(i);
    }
}

bool MemoryGraph::Save(const std::string& path) const {
    json j;
    j["version"] = 1;
    for (const MemNode& n : nodes_)
        j["nodes"].push_back({{"k", int(n.kind)}, {"l", n.label}, {"t0", n.firstT}, {"t1", n.lastT}, {"seen", n.seen},
                              {"sal", n.salience}, {"com", n.community}, {"p", {n.pos.x, n.pos.y, n.pos.z}}, {"hp", n.hasPos}});
    for (const MemEdge& e : edges_)
        if (e.alive)
            j["edges"].push_back({{"a", e.from}, {"b", e.to}, {"r", int(e.rel)}, {"s", int(e.src)}, {"sup", e.support},
                                  {"tr", e.trials}, {"w", e.weight}, {"t0", e.firstT}, {"t1", e.lastT}});
    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(1);
    // a snapshot supersedes the journal
    if (!journalPath_.empty()) std::ofstream(journalPath_, std::ios::trunc);
    const_cast<MemoryGraph*>(this)->pending_.clear();
    return true;
}

bool MemoryGraph::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    json j;
    try { f >> j; } catch (...) { return false; }
    nodes_.clear();
    edges_.clear();
    const json nodes = j.value("nodes", json::array());
    const json edges = j.value("edges", json::array());
    for (const json& n : nodes) {
        MemNode m;
        m.id = static_cast<uint32_t>(nodes_.size()) + 1;
        m.kind = NodeKind(n.value("k", 0));
        m.label = n.value("l", std::string());
        m.firstT = n.value("t0", 0.0);
        m.lastT = n.value("t1", 0.0);
        m.seen = n.value("seen", 1u);
        m.salience = n.value("sal", 0.5f);
        m.community = n.value("com", -1);
        const json p = n.value("p", json::array({0, 0, 0}));
        m.pos = {p[0].get<float>(), p[1].get<float>(), p[2].get<float>()};
        m.hasPos = n.value("hp", false);
        nodes_.push_back(m);
    }
    for (const json& e : edges) {
        MemEdge m;
        m.from = e.value("a", 0u); m.to = e.value("b", 0u);
        m.rel = Rel(e.value("r", 0)); m.src = Source(e.value("s", 0));
        m.support = e.value("sup", 0u); m.trials = e.value("tr", 0u);
        m.weight = e.value("w", 1.f);
        m.firstT = e.value("t0", 0.0); m.lastT = e.value("t1", 0.0);
        if (Node(m.from) && Node(m.to)) edges_.push_back(m);
    }
    Reindex();
    // replay changes made since that snapshot
    if (!journalPath_.empty()) {
        std::ifstream jf(journalPath_);
        const std::string keep = journalPath_;
        journalPath_.clear();   // don't re-log while replaying
        std::string line;
        while (std::getline(jf, line)) {
            if (line.empty()) continue;
            json op;
            try { op = json::parse(line); } catch (...) { continue; }
            const std::string o = op.value("op", "");
            if (o == "n") Ensure(NodeKind(op.value("k", 0)), op.value("l", ""), op.value("t", 0.0));
            else if (o == "e") Evidence(op.value("a", 0u), op.value("b", 0u), Rel(op.value("r", 0)), Source(op.value("s", 0)), op.value("h", true), op.value("t", 0.0), op.value("w", 1.f));
            else if (o == "s") Touch(op.value("i", 0u), op.value("t", 0.0), op.value("b", 0.f));
            else if (o == "r") Rename(op.value("i", 0u), op.value("l", ""));
            else if (o == "c") Consolidate(op.value("t", 0.0), op.value("h", 0.f));
        }
        journalPath_ = keep;
    }
    ++version_;
    return true;
}

bool MemoryGraph::ExportHtml(const std::string& path, double now) const {
    json data;
    data["nodes"] = json::array();
    data["edges"] = json::array();
    for (const MemNode& n : nodes_)
        data["nodes"].push_back({{"id", n.id}, {"label", n.label}, {"kind", KindName(n.kind)}, {"sal", n.salience}, {"com", n.community},
                                 {"desc", Describe(n.id, now)}});
    for (const MemEdge& e : edges_)
        if (e.alive) data["edges"].push_back({{"a", e.from}, {"b", e.to}, {"rel", RelName(e.rel)}, {"src", SourceName(e.src)}, {"conf", e.Confidence()}});
    std::ofstream f(path);
    if (!f) return false;
    f << R"(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jenny's Memory</title><style>
:root{--bg:#f6f5f2;--fg:#1d1d1f;--mut:#6b6b70;--line:#c9c6bf}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#17171a;--fg:#ececef;--mut:#9a9aa2;--line:#3a3a40}}
:root[data-theme="dark"]{--bg:#17171a;--fg:#ececef;--mut:#9a9aa2;--line:#3a3a40}
body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif}
header{padding:12px 16px;display:flex;gap:16px;align-items:baseline;flex-wrap:wrap}h1{font-size:18px;margin:0}
#info{padding:0 16px 12px;color:var(--mut);min-height:2.6em}canvas{display:block;width:100%;height:calc(100vh - 110px)}
.k{display:inline-flex;gap:6px;align-items:center;color:var(--mut);font-size:12px}.k i{width:10px;height:10px;border-radius:50%;display:inline-block}
</style></head><body><header><h1>Jenny's memory</h1><span id="stats" class="k"></span><span id="legend"></span></header>
<div id="info">Click a node. Solid = observed, dashed = inferred, dotted = ambiguous.</div><canvas id="c"></canvas><script>
const D=)" << data.dump() << R"(;
const col={thing:'#3b82c4',place:'#7a5cc4',action:'#d0782c',effect:'#c43b6e',feeling:'#2c9d7a',idea:'#8a8f3b',self:'#555'};
document.getElementById('stats').textContent=D.nodes.length+' memories · '+D.edges.length+' links';
document.getElementById('legend').innerHTML=Object.entries(col).map(([k,v])=>`<span class="k"><i style="background:${v}"></i>${k}</span>`).join(' ');
const cv=document.getElementById('c'),cx=cv.getContext('2d');let W,H;function rs(){W=cv.width=cv.clientWidth*devicePixelRatio;H=cv.height=cv.clientHeight*devicePixelRatio}rs();addEventListener('resize',rs);
const N=D.nodes.map((n,i)=>({...n,x:W/2+Math.cos(i*2.4)*i*6,y:H/2+Math.sin(i*2.4)*i*6,vx:0,vy:0}));const byId={};N.forEach(n=>byId[n.id]=n);
const E=D.edges.filter(e=>byId[e.a]&&byId[e.b]);let sel=null;
function step(){for(const a of N){for(const b of N){if(a===b)continue;const dx=a.x-b.x,dy=a.y-b.y,d2=dx*dx+dy*dy+50;a.vx+=dx/d2*900;a.vy+=dy/d2*900}a.vx+=(W/2-a.x)*0.0008;a.vy+=(H/2-a.y)*0.0008}
for(const e of E){const a=byId[e.a],b=byId[e.b],dx=b.x-a.x,dy=b.y-a.y,d=Math.hypot(dx,dy)||1,f=(d-90*devicePixelRatio)*0.004*(0.3+e.conf);a.vx+=dx/d*f;a.vy+=dy/d*f;b.vx-=dx/d*f;b.vy-=dy/d*f}
for(const n of N){n.vx*=0.85;n.vy*=0.85;const v=Math.hypot(n.vx,n.vy);if(v>20){n.vx*=20/v;n.vy*=20/v}n.x+=n.vx;n.y+=n.vy}}
let T={s:1,x:0,y:0};function fit(){let a=1e9,b=1e9,c=-1e9,d=-1e9;for(const n of N){a=Math.min(a,n.x);b=Math.min(b,n.y);c=Math.max(c,n.x);d=Math.max(d,n.y)}const pad=140*devicePixelRatio;const s=Math.min((W-pad)/Math.max(1,c-a),(H-pad*0.5)/Math.max(1,d-b),2);T={s,x:W/2-(a+c)/2*s,y:H/2-(b+d)/2*s}}
const P=n=>[n.x*T.s+T.x,n.y*T.s+T.y];
function draw(){fit();const s=getComputedStyle(document.documentElement);cx.clearRect(0,0,W,H);cx.lineWidth=devicePixelRatio;
for(const e of E){const a=byId[e.a],b=byId[e.b];cx.strokeStyle=s.getPropertyValue('--line');cx.globalAlpha=0.35+0.65*e.conf;cx.setLineDash(e.src==='ambiguous'?[2,4]:e.src==='inferred'?[6,4]:[]);const pa=P(a),pb=P(b);cx.beginPath();cx.moveTo(pa[0],pa[1]);cx.lineTo(pb[0],pb[1]);cx.stroke()}
cx.setLineDash([]);cx.globalAlpha=1;cx.font=(11*devicePixelRatio)+'px system-ui';
for(const n of N){const r=(4+8*n.sal)*devicePixelRatio,[px,py]=P(n);cx.fillStyle=col[n.kind]||'#888';cx.beginPath();cx.arc(px,py,r,0,7);cx.fill();if(n===sel){cx.strokeStyle=s.getPropertyValue('--fg');cx.stroke()}
cx.fillStyle=s.getPropertyValue('--fg');cx.fillText(n.label,px+r+3,py+4)}}
function loop(){step();draw();requestAnimationFrame(loop)}loop();
cv.addEventListener('click',ev=>{const r=cv.getBoundingClientRect(),x=(ev.clientX-r.left)*devicePixelRatio,y=(ev.clientY-r.top)*devicePixelRatio;let b=null,bd=1e9;for(const n of N){const [px,py]=P(n),d=Math.hypot(px-x,py-y);if(d<bd){bd=d;b=n}}
sel=bd<30*devicePixelRatio?b:null;document.getElementById('info').textContent=sel?(sel.kind+': '+sel.desc):'Click a node. Solid = observed, dashed = inferred, dotted = ambiguous.'});
</script></body></html>)";
    return true;
}

} // namespace eon
