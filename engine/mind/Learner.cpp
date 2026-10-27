#include "Learner.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace eon {

std::string ColorWord(const Rgb& c) {
    const float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const float v = mx, s = mx > 1e-4f ? (mx - mn) / mx : 0.f;
    if (s < 0.18f) return v > 0.85f ? "white" : v > 0.62f ? "light grey" : v > 0.35f ? "grey" : v > 0.15f ? "dark grey" : "black";
    float h;
    const float d = mx - mn;
    if (mx == r) h = 60.f * std::fmod((g - b) / d, 6.f);
    else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
    else h = 60.f * ((r - g) / d + 4.f);
    if (h < 0) h += 360.f;
    if ((h < 45.f || h > 330.f) && v < 0.65f && s < 0.75f && h > 15.f) return "brown";
    if (h < 15.f || h >= 340.f) return v < 0.45f ? "dark red" : "red";
    if (h < 45.f) return v < 0.7f ? "brown" : "orange";
    if (h < 70.f) return s < 0.4f ? "cream" : "yellow";
    if (h < 160.f) return "green";
    if (h < 200.f) return "teal";
    if (h < 255.f) return "blue";
    if (h < 290.f) return "purple";
    return "pink";
}

namespace {
bool Inside(float x, float a, float b, float m = 0.02f) { return x >= a - m && x <= b + m; }
}

std::string Learner::Name(const SeenThing& s) {
    const Vec3 sz = s.hi - s.lo;
    float d[3] = {sz.x, sz.y, sz.z};
    std::sort(d, d + 3);
    std::string base;
    const std::string col = ColorWord(s.color);
    if (s.wide) {
        // big surfaces: floor, ceiling or wall, said the way a person would
        const bool horizontal = sz.y < 0.4f;
        base = horizontal ? (s.pos.y < 0.3f ? col + " floor" : col + " ceiling") : col + " wall";
    } else {
        const float big = d[2];
        const char* size = big < 0.05f ? "tiny " : big < 0.15f ? "small " : big < 0.5f ? "" : big < 1.5f ? "big " : "huge ";
        const char* shape = d[2] > 4.f * d[1] ? (d[1] < 0.02f ? "cord" : "stick") : d[0] < 0.3f * d[1] ? (big > 0.5f ? "slab" : "plate") : "block";
        base = std::string(size) + col + " " + shape;
    }
    int& n = nameUse_[base];
    ++n;
    return n == 1 ? base : base + " " + std::to_string(n);
}

uint32_t Learner::NearMouth(const Percept& p) const {
    // what's at her mouth: something in her hand first, else the nearest thing just in front of her face
    for (const HandFeel& h : p.hands) if (h.holding && NodeOf(h.held)) return NodeOf(h.held);
    uint32_t best = 0;
    float bd = 0.5f;
    for (const SeenThing& s : p.seen) {
        if (s.wide) continue;
        const float d = Distance(s.pos, p.eye);
        if (d < bd && NodeOf(s.track)) { bd = d; best = NodeOf(s.track); }
    }
    return best;
}

void Learner::SelfHappened(const std::string& what, double t) {
    if (!me_) return;
    const uint32_t e = graph.Ensure(NodeKind::Effect, what, t);
    graph.Touch(e, t, 0.3f);
    graph.Evidence(e, me_, Rel::Affects, Source::Observed, true, t);
    std::vector<uint32_t> cands;
    for (const Recent& r : recent_)
        if (t - r.t <= causeWindow && std::find(cands.begin(), cands.end(), r.action) == cands.end()) cands.push_back(r.action);
    const float w = cands.empty() ? 1.f : 1.f / float(cands.size());
    for (uint32_t a : cands) graph.Evidence(a, e, Rel::Causes, cands.size() == 1 ? Source::Inferred : Source::Ambiguous, true, t, w);
}

uint32_t Learner::NodeOf(uint32_t track) const {
    auto it = tracks_.find(track);
    return it == tracks_.end() ? 0 : it->second.node;
}

void Learner::SetBackstory(const std::vector<std::string>& facts, double t) {
    me_ = graph.Ensure(NodeKind::Self, "me", t);
    graph.Node(me_)->salience = 1.f;
    for (const std::string& f : facts) {
        const uint32_t id = graph.Ensure(NodeKind::Idea, f, t);
        graph.Evidence(me_, id, Rel::Knows, Source::Told, true, t);
    }
}

void Learner::Property(uint32_t node, const std::string& prop, bool held, double t, Source s) {
    if (!node) return;
    const uint32_t p = graph.Ensure(NodeKind::Idea, prop, t);
    graph.Evidence(node, p, Rel::Has, s, held, t);
}

void Learner::Act(const std::string& verb, uint32_t thing, double t) {
    const MemNode* tn = graph.Node(thing);
    if (!tn) return;
    const uint32_t a = graph.Ensure(NodeKind::Action, verb + " " + tn->label, t);
    graph.Touch(a, t, 0.1f);
    graph.Evidence(a, thing, Rel::Targets, Source::Observed, true, t);
    recent_.push_back({a, t});
    // every known effect of this action is now being tested again
    for (const MemEdge* e : graph.Out(a, Rel::Causes)) pending_.push_back({a, e->to, t + causeWindow, false});
}

void Learner::Happened(const std::string& what, uint32_t thing, double t) {
    const MemNode* tn = graph.Node(thing);
    if (!tn) return;
    const uint32_t e = graph.Ensure(NodeKind::Effect, tn->label + " " + what, t);
    graph.Touch(e, t, 0.3f);
    graph.Touch(thing, t, 0.2f);   // things that do something are interesting
    graph.Evidence(e, thing, Rel::Affects, Source::Observed, true, t);
    std::vector<uint32_t> cands;
    for (const Recent& r : recent_)
        if (t - r.t <= causeWindow && std::find(cands.begin(), cands.end(), r.action) == cands.end()) cands.push_back(r.action);
    const float w = cands.empty() ? 1.f : 1.f / float(cands.size());
    for (uint32_t a : cands) graph.Evidence(a, e, Rel::Causes, cands.size() == 1 ? Source::Inferred : Source::Ambiguous, true, t, w);
    for (Pending& p : pending_) if (p.effect == e && std::find(cands.begin(), cands.end(), p.action) != cands.end()) p.met = true;
}

void Learner::Spatial(const Percept& p) {
    const double t = p.t;
    for (const SeenThing& a : p.seen) {
        if (a.wide) continue;
        const uint32_t na = NodeOf(a.track);
        for (const SeenThing& b : p.seen) {
            if (a.track == b.track) continue;
            const uint32_t nb = NodeOf(b.track);
            // resting on: its bottom at the other's top, and over it
            if (std::fabs(a.lo.y - b.hi.y) < 0.04f && Inside(a.pos.x, b.lo.x, b.hi.x) && Inside(a.pos.z, b.lo.z, b.hi.z)) {
                const MemEdge* e = graph.Edge(na, nb, Rel::On);
                if (!e || t - e->lastT > 10.0) graph.Evidence(na, nb, Rel::On, Source::Observed, true, t);
            }
            if (!b.wide && na < nb && Distance(a.pos, b.pos) < 0.6f) {
                const MemEdge* e = graph.Edge(na, nb, Rel::Near);
                if (!e || t - e->lastT > 10.0) graph.Evidence(na, nb, Rel::Near, Source::Observed, true, t);
            }
        }
    }
}

void Learner::Observe(const Percept& p) {
    const double t = p.t;
    if (!me_) me_ = graph.Ensure(NodeKind::Self, "me", t);

    // tests that ran out: she did it and the usual thing didn't happen
    for (Pending& q : pending_) {
        if (q.met || t < q.deadline) continue;
        if (const MemEdge* e = graph.Edge(q.action, q.effect, Rel::Causes)) graph.Evidence(q.action, q.effect, Rel::Causes, e->src, false, t, e->weight);
        q.met = true;
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](const Pending& q) { return q.met; }), pending_.end());
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const Recent& r) { return t - r.t > causeWindow; }), recent_.end());

    // hands first: actions this instant can explain changes she sees this instant
    for (int i = 0; i < 2; ++i) {
        const HandFeel& h = p.hands[i];
        HandMem& hm = hands_[i];
        auto nodeFor = [&](uint32_t track) {
            TrackMem& m = tracks_[track];
            if (!m.node) m.node = graph.Ensure(NodeKind::Thing, "something I felt", t);   // touched before she ever saw it
            return m.node;
        };
        const uint32_t touched = h.touching ? nodeFor(h.touching) : 0;
        if (touched && h.reaching && !h.holding && h.force > 1.5f && (h.touching != hm.touching || !hm.pressed)) {
            Act(h.pointing ? "press" : "push", touched, t);
            hm.pressed = true;
            hm.wasPoint = h.pointing;
        }
        if (hm.pressed && (!h.touching || !h.reaching || h.force < 0.5f)) {
            if (const uint32_t was = NodeOf(hm.touching)) Act(hm.wasPoint ? "stop pressing" : "stop pushing", was, t);
            hm.pressed = false;
        }
        if (hm.pressed) hm.wasPoint = h.pointing || hm.wasPoint;
        if (!hm.pressed) hm.wasPoint = h.pointing;
        if (h.holding && !hm.holding) {
            const uint32_t n = nodeFor(h.held);
            Act("grab", n, t);
            hm.grabPos = tracks_[h.held].pos;
            hm.moved = false;
            Property(n, h.weight < 0.3f ? "very light" : h.weight < 3.f ? "light" : h.weight < 10.f ? "heavy" : "very heavy", true, t);
        }
        if (h.holding) {
            const uint32_t n = nodeFor(h.held);
            const TrackMem& m = tracks_[h.held];
            if (!hm.moved && m.lastSeen >= 0 && Distance(m.pos, hm.grabPos) > 0.04f) {
                Act("move", n, t);
                hm.moved = true;
                Property(n, "moves when I pull it", true, t);
            }
        }
        if (!h.holding && hm.holding && hm.held) Act("let go of", nodeFor(hm.held), t);
        hm.touching = h.touching;
        hm.holding = h.holding;
        hm.held = h.held;
    }

    // what she sees
    for (const SeenThing& s : p.seen) {
        TrackMem& m = tracks_[s.track];
        const bool fresh = !m.node || (graph.Node(m.node) && graph.Node(m.node)->label == "something I felt");
        if (fresh) {
            const std::string name = Name(s);
            if (m.node) graph.Rename(m.node, name);   // the thing she felt now has a look
            else m.node = graph.Ensure(NodeKind::Thing, name, t);
            graph.Touch(m.node, t, s.wide ? 0.f : 0.1f);
            if (s.wide) graph.Node(m.node)->salience = 0.15f;
            m.lit = s.glow > 0.35f;
            m.pos = m.restPos = s.pos;
            if (m.lit) Property(m.node, "glows", true, t);
        } else {
            graph.Touch(m.node, t);
        }
        MemNode* n = graph.Node(m.node);
        n->pos = s.pos;
        n->hasPos = true;
        const bool recentlySeen = m.lastSeen >= 0 && t - m.lastSeen < 1.5;
        const bool lit = m.lit ? s.glow > 0.2f : s.glow > 0.35f;
        // a light that changed while she glanced away a moment still counts (she notices it on looking back)
        if (!fresh && lit != m.lit && m.lastSeen >= 0 && t - m.lastSeen < 6.0) {
            Happened(lit ? "lights up" : "goes dark", m.node, t);
            Property(m.node, "can light up", true, t);
        }
        m.lit = lit;
        bool heldByHer = false;
        for (const HandFeel& h : p.hands) heldByHer |= h.holding && h.held == s.track;
        if (heldByHer) m.restPos = s.pos;
        else if (recentlySeen && Distance(s.pos, m.restPos) > moveThreshold) {
            Happened("moved", m.node, t);
            Property(m.node, "can move", true, t);
            m.restPos = s.pos;
        } else if (!recentlySeen) {
            m.restPos = s.pos;
        }
        m.pos = s.pos;
        m.lo = s.lo;
        m.hi = s.hi;
        m.wide = s.wide;
        m.lastSeen = t;
    }

    // body feelings
    for (const auto& [name, v] : p.feelings) {
        const bool active = v > 0.35f;
        bool& was = feeling_[name];
        if (active && !was) {
            const uint32_t f = graph.Ensure(NodeKind::Feeling, name, t);
            graph.Touch(f, t, 0.3f);
            graph.Evidence(me_, f, Rel::Felt, Source::Observed, true, t);
        }
        was = active;
    }
    for (auto& [name, was] : feeling_) {
        bool still = false;
        for (const auto& f : p.feelings) still |= f.first == name && f.second > 0.25f;
        if (!still) was = false;
    }

    // mouth and body: drinking/eating from something, and what it does for her
    const bool thirsty = std::any_of(p.feelings.begin(), p.feelings.end(), [](const auto& f) { return f.first == "thirsty" && f.second > 0.2f; });
    const bool hungry = std::any_of(p.feelings.begin(), p.feelings.end(), [](const auto& f) { return f.first == "hungry" && f.second > 0.2f; });
    if (p.drankMl > 0.f) {
        if (t - lastDrink_ > 3.0) {
            const uint32_t src = NearMouth(p);
            if (src) Act("drink from", src, t);
            else { const uint32_t a = graph.Ensure(NodeKind::Action, "drink", t); graph.Touch(a, t, 0.1f); recent_.push_back({a, t}); }
            if (thirsty) SelfHappened("I'm less thirsty", t);
        }
        lastDrink_ = t;
    }
    if (p.biteG > 0.f) {
        if (t - lastBite_ > 3.0) {
            const uint32_t food = NearMouth(p);
            if (food) { Act("bite", food, t); Property(food, "can be eaten", true, t); }
            if (hungry) SelfHappened("I'm less hungry", t);
        }
        lastBite_ = t;
    }
    if (p.peeing && !wasPeeing_) {
        const uint32_t a = graph.Ensure(NodeKind::Action, "pee", t);
        graph.Touch(a, t, 0.1f);
        recent_.push_back({a, t});
        SelfHappened("my bladder is relieved", t);
    }
    wasPeeing_ = p.peeing;

    if (t - lastSpatial_ > 2.0) { Spatial(p); lastSpatial_ = t; }
}

std::string Learner::Context(const Percept& p, int tokenBudget) const {
    std::ostringstream now;
    std::vector<uint32_t> seeds;
    if (me_) seeds.push_back(me_);
    now << "Now: I'm " << StanceName(p.stance);
    int shown = 0;
    for (const SeenThing& s : p.seen) {
        if (s.wide) continue;
        const uint32_t n = NodeOf(s.track);
        if (!n) continue;
        seeds.push_back(n);
        if (shown < 6) now << (shown ? ", " : ". I see ") << graph.Node(n)->label << (s.glow > 0.35f ? " (glowing)" : "");
        ++shown;
        if (shown >= 10) break;
    }
    for (int i = 0; i < 2; ++i) {
        const HandFeel& h = p.hands[i];
        if (h.holding && NodeOf(h.held)) { now << ". My " << (i == 0 ? "left" : "right") << " hand holds " << graph.Node(NodeOf(h.held))->label; seeds.push_back(NodeOf(h.held)); }
        else if (h.touching && NodeOf(h.touching)) { now << ". My " << (i == 0 ? "left" : "right") << " hand touches " << graph.Node(NodeOf(h.touching))->label; seeds.push_back(NodeOf(h.touching)); }
    }
    bool first = true;
    for (const auto& [name, v] : p.feelings) {
        if (v < 0.35f) continue;
        now << (first ? ". I feel " : ", ") << name;
        first = false;
        if (const uint32_t f = graph.Find(NodeKind::Feeling, name)) seeds.push_back(f);
    }
    for (const MemNode& n : graph.Nodes()) if (n.kind == NodeKind::Effect && p.t - n.lastT < 15.0) seeds.push_back(n.id);
    now << ".\n";
    const std::string head = now.str();
    const int left = std::max(16, tokenBudget - static_cast<int>(head.size() / 4));
    return head + graph.Context(seeds, p.t, left);
}

} // namespace eon
