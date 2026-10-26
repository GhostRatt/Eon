#include "Circuit.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace eon {

namespace {

constexpr double kGmin = 1e-9;          // tiny leak to keep floating nets solvable
constexpr double kSwitchContactR = 0.01; // closed contact resistance (ohms)
constexpr double kMinR = 1e-6;

struct DSU {
    std::vector<int> p;
    explicit DSU(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int Find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
    void Union(int a, int b) { a = Find(a); b = Find(b); if (a != b) p[b] = a; }
};

// Solves A x = b in place (A is n*n row-major). Partial pivoting.
void GaussSolve(std::vector<double>& A, std::vector<double>& b, int n) {
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double best = std::abs(A[col * n + col]);
        for (int r = col + 1; r < n; ++r) {
            const double v = std::abs(A[r * n + col]);
            if (v > best) { best = v; pivot = r; }
        }
        if (best < 1e-18) continue;
        if (pivot != col) {
            for (int c = 0; c < n; ++c) std::swap(A[col * n + c], A[pivot * n + c]);
            std::swap(b[col], b[pivot]);
        }
        const double inv = 1.0 / A[col * n + col];
        for (int r = col + 1; r < n; ++r) {
            const double f = A[r * n + col] * inv;
            if (f == 0.0) continue;
            for (int c = col; c < n; ++c) A[r * n + c] -= f * A[col * n + c];
            b[r] -= f * b[col];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        double s = b[r];
        for (int c = r + 1; c < n; ++c) s -= A[r * n + c] * b[c];
        const double d = A[r * n + r];
        b[r] = std::abs(d) < 1e-18 ? 0.0 : s / d;
    }
}

} // namespace

double BatteryEmf(const Part& b) {
    const double soc = b.capacityAh > 0.0 ? std::clamp(b.chargeAh / b.capacityAh, 0.0, 1.0) : 1.0;
    const double frac = soc < 0.05 ? (soc / 0.05) * 0.7625 : 0.75 + 0.25 * soc;
    return b.voltage * frac;
}

int Circuit::AddPart(const Part& p) {
    parts_.push_back(p);
    return static_cast<int>(parts_.size()) - 1;
}

void Circuit::Connect(TerminalRef a, TerminalRef b) {
    for (const Connection& c : connections_) {
        const bool same = (TerminalId(c.a) == TerminalId(a) && TerminalId(c.b) == TerminalId(b)) ||
                          (TerminalId(c.a) == TerminalId(b) && TerminalId(c.b) == TerminalId(a));
        if (same) return;
    }
    connections_.push_back({a, b});
}

void Circuit::Disconnect(TerminalRef a, TerminalRef b) {
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(), [&](const Connection& c) {
        return (TerminalId(c.a) == TerminalId(a) && TerminalId(c.b) == TerminalId(b)) ||
               (TerminalId(c.a) == TerminalId(b) && TerminalId(c.b) == TerminalId(a));
    }), connections_.end());
}

int Circuit::NodeOf(TerminalRef t) const {
    const int id = TerminalId(t);
    return id >= 0 && id < (int)terminalNode_.size() ? terminalNode_[id] : -1;
}

void Circuit::BuildNodes() {
    const int terminals = PartCount() * 2;
    DSU dsu(terminals);
    for (const Connection& c : connections_) {
        const int a = TerminalId(c.a), b = TerminalId(c.b);
        if (a >= 0 && a < terminals && b >= 0 && b < terminals) dsu.Union(a, b);
    }

    terminalNode_.assign(terminals, -1);
    std::vector<int> rootToNode(terminals, -1);
    nodeCount_ = 0;
    for (int t = 0; t < terminals; ++t) {
        const int r = dsu.Find(t);
        if (rootToNode[r] < 0) rootToNode[r] = nodeCount_++;
        terminalNode_[t] = rootToNode[r];
    }
}

void Circuit::Solve() {
    const int n = nodeCount_;
    std::vector<double> G(static_cast<size_t>(n) * n, 0.0);
    std::vector<double> I(n, 0.0);

    auto stampG = [&](int a, int b, double g) {
        G[a * n + a] += g; G[b * n + b] += g;
        G[a * n + b] -= g; G[b * n + a] -= g;
    };
    // Series source e in a branch A->B with conductance g: branch current = g*(vA - vB - e)
    auto stampSource = [&](int a, int b, double g, double e) {
        I[a] += g * e; I[b] -= g * e;
    };

    for (int k = 0; k < n; ++k) G[k * n + k] += kGmin;

    for (int idx = 0; idx < PartCount(); ++idx) {
        Part& p = parts_[idx];
        const int a = terminalNode_[idx * 2];
        const int b = terminalNode_[idx * 2 + 1];
        if (a == b) continue; // shorted onto itself: no voltage across

        switch (p.kind) {
        case PartKind::Battery: {
            const double g = 1.0 / std::max(p.internalR, 1e-4);
            stampG(a, b, g);
            stampSource(a, b, g, BatteryEmf(p)); // open circuit: vA - vB = emf
            break;
        }
        case PartKind::Switch:
            if (p.closed) stampG(a, b, 1.0 / kSwitchContactR);
            break;
        case PartKind::Motor: {
            const double g = 1.0 / std::max(p.resistance, kMinR);
            stampG(a, b, g);
            stampSource(a, b, g, p.motorK * p.motorSpeed);
            break;
        }
        default:
            stampG(a, b, 1.0 / std::max(p.resistance, kMinR));
            break;
        }
    }

    GaussSolve(G, I, n);
    nodeV_ = I;

    for (int idx = 0; idx < PartCount(); ++idx) {
        Part& p = parts_[idx];
        const int a = terminalNode_[idx * 2];
        const int b = terminalNode_[idx * 2 + 1];
        p.v = nodeV_[a] - nodeV_[b];
        p.i = p.power = p.brightness = p.torque = 0.0;

        switch (p.kind) {
        case PartKind::Battery: {
            if (a == b) { p.i = BatteryEmf(p) / std::max(p.internalR, 1e-4); break; } // dead short
            const double g = 1.0 / std::max(p.internalR, 1e-4);
            p.i = (BatteryEmf(p) - p.v) * g;
            p.power = p.v * p.i;
            break;
        }
        case PartKind::Switch:
            p.i = p.closed ? p.v / kSwitchContactR : 0.0;
            p.power = p.v * p.i;
            break;
        case PartKind::Motor: {
            const double g = 1.0 / std::max(p.resistance, kMinR);
            p.i = (p.v - p.motorK * p.motorSpeed) * g;
            p.torque = p.motorK * p.i;
            p.power = p.v * p.i;
            break;
        }
        case PartKind::Lamp:
            p.i = p.v / std::max(p.resistance, kMinR);
            p.power = p.v * p.i;
            p.brightness = std::clamp(p.power / std::max(p.ratedPower, 1e-6), 0.0, 1.0);
            break;
        default:
            p.i = p.v / std::max(p.resistance, kMinR);
            p.power = p.v * p.i;
            break;
        }
    }
}

void Circuit::Step(double dt) {
    BuildNodes();
    if (nodeCount_ == 0) return;
    Solve();

    for (Part& p : parts_) {
        if (p.kind == PartKind::Battery && p.capacityAh > 0.0 && p.i > 0.0) {
            p.chargeAh = std::max(0.0, p.chargeAh - p.i * dt / 3600.0);
        }
    }
}

} // namespace eon
