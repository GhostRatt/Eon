#include <cmath>
#include <cstdio>
#include <string>

#include "core/Circuit.h"
#include "core/World.h"
#include "core/Zone.h"

using namespace eon;

static int g_fail = 0, g_pass = 0;

#define CHECK_NEAR(a, b, tol) do { \
    const double _a = (a), _b = (b); \
    if (std::abs(_a - _b) > (tol)) { std::printf("FAIL %s:%d  %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #a, _a, _b); ++g_fail; } \
    else ++g_pass; } while (0)

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } else ++g_pass; } while (0)

static Part Battery(double v, double r, double ah = 0.0) {
    Part p; p.kind = PartKind::Battery; p.voltage = v; p.internalR = r; p.capacityAh = ah; p.chargeAh = ah; return p;
}
static Part Res(double r, PartKind k = PartKind::Resistor) { Part p; p.kind = k; p.resistance = r; return p; }
static Part Sw(bool closed) { Part p; p.kind = PartKind::Switch; p.closed = closed; return p; }

static void TestOpenBattery() {
    Circuit c;
    const int b = c.AddPart(Battery(9.0, 1.0));
    c.Step(0.0);
    CHECK_NEAR(c.GetPart(b).v, 9.0, 1e-3);
    CHECK_NEAR(c.GetPart(b).i, 0.0, 1e-6);
}

static void TestSimpleLoop() {
    // 9V, 1 ohm internal, 8 ohm load -> 1 A, 8 V across load
    Circuit c;
    const int b = c.AddPart(Battery(9.0, 1.0));
    const int r = c.AddPart(Res(8.0));
    c.Connect({b, 0}, {r, 0});
    c.Connect({r, 1}, {b, 1});
    c.Step(0.0);
    CHECK_NEAR(c.GetPart(b).i, 1.0, 1e-4);
    CHECK_NEAR(c.GetPart(r).i, 1.0, 1e-4);
    CHECK_NEAR(c.GetPart(r).v, 8.0, 1e-3);
    CHECK_NEAR(c.GetPart(r).power, 8.0, 1e-3);
}

static void TestSwitchAndLamp() {
    Circuit c;
    const int b = c.AddPart(Battery(12.0, 0.05));
    Part lamp = Res(28.8, PartKind::Lamp); lamp.ratedPower = 5.0;
    const int l = c.AddPart(lamp);
    const int s = c.AddPart(Sw(false));
    const int w1 = c.AddPart(Res(0.02, PartKind::Wire));
    const int w2 = c.AddPart(Res(0.02, PartKind::Wire));
    c.Connect({b, 0}, {w1, 0});
    c.Connect({w1, 1}, {s, 0});
    c.Connect({s, 1}, {l, 0});
    c.Connect({l, 1}, {w2, 0});
    c.Connect({w2, 1}, {b, 1});

    c.Step(0.0);
    CHECK_NEAR(c.GetPart(l).brightness, 0.0, 1e-6);

    c.GetPart(s).closed = true;
    c.Step(0.0);
    CHECK(c.GetPart(l).brightness > 0.98);
    CHECK_NEAR(c.GetPart(l).i, 12.0 / (28.8 + 0.05 + 0.04 + 0.01), 1e-3);
}

static void TestUnconnectedWireDoesNothing() {
    // Wire touching only the + terminal: no loop, no current
    Circuit c;
    const int b = c.AddPart(Battery(9.0, 1.0));
    const int l = c.AddPart(Res(10.0, PartKind::Lamp));
    c.Connect({b, 0}, {l, 0});
    c.Step(0.0);
    CHECK_NEAR(c.GetPart(l).i, 0.0, 1e-6);
}

static void TestParallel() {
    Circuit c;
    const int b = c.AddPart(Battery(10.0, 0.0001));
    const int r1 = c.AddPart(Res(10.0));
    const int r2 = c.AddPart(Res(10.0));
    c.Connect({b, 0}, {r1, 0}); c.Connect({b, 0}, {r2, 0});
    c.Connect({b, 1}, {r1, 1}); c.Connect({b, 1}, {r2, 1});
    c.Step(0.0);
    CHECK_NEAR(c.GetPart(b).i, 2.0, 1e-3);
}

static void TestShortCircuit() {
    Circuit c;
    const int b = c.AddPart(Battery(9.0, 1.5));
    const int w = c.AddPart(Res(0.02, PartKind::Wire));
    c.Connect({b, 0}, {w, 0});
    c.Connect({w, 1}, {b, 1});
    c.Step(0.0);
    CHECK_NEAR(c.GetPart(b).i, 9.0 / 1.52, 1e-3);
    CHECK(c.GetPart(b).power < 1.0);          // almost nothing delivered outside
    CHECK(c.GetPart(b).i * c.GetPart(b).i * 1.5 > 50.0); // heats internally
}

static void TestBatteryDrains() {
    Circuit c;
    const int b = c.AddPart(Battery(9.0, 1.0, 0.01)); // 10 mAh
    const int r = c.AddPart(Res(8.0));
    c.Connect({b, 0}, {r, 0});
    c.Connect({r, 1}, {b, 1});
    for (int k = 0; k < 120 * 60; ++k) c.Step(1.0 / 120.0); // 60 s
    CHECK(c.GetPart(b).chargeAh < 0.01);
    CHECK(c.GetPart(b).v < 8.0);
    for (int k = 0; k < 120 * 120; ++k) c.Step(1.0 / 120.0);
    CHECK_NEAR(c.GetPart(b).chargeAh, 0.0, 1e-6);
    CHECK(c.GetPart(r).i < 0.05);
}

static void TestMotor() {
    Circuit c;
    const int b = c.AddPart(Battery(6.0, 0.1));
    Part m; m.kind = PartKind::Motor; m.resistance = 2.0; m.motorK = 0.01;
    const int mi = c.AddPart(m);
    c.Connect({b, 0}, {mi, 0});
    c.Connect({mi, 1}, {b, 1});
    c.Step(0.0);
    const double stallTorque = c.GetPart(mi).torque;
    CHECK(stallTorque > 0.02);
    c.GetPart(mi).motorSpeed = 300.0; // spinning: back-EMF 3 V
    c.Step(0.0);
    CHECK(c.GetPart(mi).torque < stallTorque * 0.6);
}

static void TestZoneRoundTrip(const std::string& contentDir) {
    for (const char* f : {"/zones/waiting_area.json", "/zones/test_zone_01.json"}) {
        World w;
        std::string err;
        const bool ok = w.Load(contentDir + f, &err);
        if (!ok) std::printf("  load error: %s\n", err.c_str());
        CHECK(ok);
        if (!ok) continue;
        Zone copy;
        CHECK(ZoneFromJson(ZoneToJson(w.GetZone()), copy));
        CHECK(copy.entities.size() == w.GetZone().entities.size());
        CHECK(copy.links.size() == w.GetZone().links.size());
    }
}

static void TestWaitingAreaLight(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/waiting_area.json"));
    int lamp = -1, sw = -1;
    for (const Entity& e : w.GetZone().entities) {
        if (e.type == "lamp") lamp = e.id;
        if (e.type == "switch") sw = e.id;
    }
    CHECK(lamp > 0 && sw > 0);
    w.Tick(0.1);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
    w.Toggle(sw);
    w.Tick(0.1);
    CHECK(w.PartOf(lamp)->brightness > 0.9);
}

static void TestTestZoneButton(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    int lamp = -1, button = -1;
    for (const Entity& e : w.GetZone().entities) {
        if (e.name == "BulbOnStand") lamp = e.id;
        if (e.type == "button") button = e.id;
    }
    w.Tick(0.1);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
    w.SetPressed(button, true);
    w.Tick(0.1);
    CHECK(w.PartOf(lamp)->brightness > 0.5);
    w.SetPressed(button, false);
    w.Tick(0.1);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
}

int main(int argc, char** argv) {
    const std::string content = argc > 1 ? argv[1] : EON_CONTENT_DIR;
    TestOpenBattery();
    TestSimpleLoop();
    TestSwitchAndLamp();
    TestUnconnectedWireDoesNothing();
    TestParallel();
    TestShortCircuit();
    TestBatteryDrains();
    TestMotor();
    TestZoneRoundTrip(content);
    TestWaitingAreaLight(content);
    TestTestZoneButton(content);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
