#pragma once

#include <cstdint>
#include <vector>

namespace eon {

// Every part has two physical terminals: A (index 0) and B (index 1).
// Battery: A = +, B = -. Motor/lamp/etc: polarity matters only for direction of current.
enum class PartKind : uint8_t { Battery, Wire, Resistor, Lamp, Switch, Motor };

struct Part {
    PartKind kind = PartKind::Resistor;

    // Electrical parameters (SI units)
    double resistance = 10.0;     // ohms (resistor, lamp, wire, motor winding)
    double voltage = 0.0;         // battery nominal volts
    double internalR = 0.1;       // battery internal resistance
    double capacityAh = 0.0;      // battery capacity
    double chargeAh = 0.0;        // battery remaining charge
    double ratedPower = 1.0;      // lamp: watts at full brightness
    double motorK = 0.01;         // motor constant: back-EMF V per rad/s and torque N·m per A
    bool closed = false;          // switch contacts

    // Inputs from physics
    double motorSpeed = 0.0;      // rad/s

    // Results (updated by Solve)
    double v = 0.0;               // voltage across A->B (vA - vB)
    double i = 0.0;               // current through part, A->B externally (battery: discharge current)
    double power = 0.0;           // watts dissipated (battery: delivered)
    double brightness = 0.0;      // lamp 0..1
    double torque = 0.0;          // motor N·m
};

struct TerminalRef {
    int part = -1;
    int terminal = 0; // 0 = A, 1 = B
};

class Circuit {
public:
    int AddPart(const Part& p);
    Part& GetPart(int index) { return parts_[index]; }
    const Part& GetPart(int index) const { return parts_[index]; }
    int PartCount() const { return static_cast<int>(parts_.size()); }

    // Physical contact between two terminals (wire clip, soldered joint, touching plates)
    void Connect(TerminalRef a, TerminalRef b);
    void Disconnect(TerminalRef a, TerminalRef b);
    void ClearConnections() { connections_.clear(); }

    // Solve currents/voltages, then drain batteries over dt seconds.
    void Step(double dt);

    // Net (node) index for a terminal after the last Step; -1 if invalid.
    int NodeOf(TerminalRef t) const;
    int NodeCount() const { return nodeCount_; }
    double NodeVoltage(int node) const { return node >= 0 && node < (int)nodeV_.size() ? nodeV_[node] : 0.0; }

private:
    struct Connection { TerminalRef a, b; };

    void BuildNodes();
    void Solve();
    static int TerminalId(TerminalRef t) { return t.part * 2 + t.terminal; }

    std::vector<Part> parts_;
    std::vector<Connection> connections_;
    std::vector<int> terminalNode_;
    std::vector<double> nodeV_;
    int nodeCount_ = 0;
};

// Battery open-circuit voltage from state of charge (simple alkaline-like curve).
double BatteryEmf(const Part& battery);

} // namespace eon
