#pragma once

#include <string>

#include "Circuit.h"
#include "Zone.h"

namespace eon {

// Runs one zone: fixed-step simulation of electricity (physics joins in step 2).
class World {
public:
    static constexpr double kFixedDt = 1.0 / 120.0;

    bool Load(const std::string& path, std::string* error = nullptr);
    void Reset();                 // rebuild runtime state from the loaded zone
    void Tick(double realDt);     // advances in fixed steps

    // Physical inputs (from physics later; from operator debug for now)
    void SetPressed(int entityId, bool pressed); // momentary button
    void Toggle(int entityId);                   // latching switch

    Zone& GetZone() { return zone_; }
    const Zone& GetZone() const { return zone_; }
    Circuit& GetCircuit() { return circuit_; }
    const Circuit& GetCircuit() const { return circuit_; }
    const Part* PartOf(int entityId) const;
    double SimTime() const { return simTime_; }
    const std::string& Path() const { return path_; }

private:
    Zone zone_;
    Circuit circuit_;
    std::string path_;
    double accumulator_ = 0.0;
    double simTime_ = 0.0;
};

} // namespace eon
