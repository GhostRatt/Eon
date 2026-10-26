#include <cmath>
#include <cstdio>
#include <string>

#include "core/Circuit.h"
#include "core/World.h"
#include "core/Physics.h"
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

static int FindByName(const World& w, const std::string& name) {
    for (const Entity& e : w.GetZone().entities) if (e.name == name) return e.id;
    return -1;
}

static bool FrameSane(const World& w) {
    RenderFrame f;
    w.BuildRenderFrame(f);
    for (const RenderPiece& p : f.pieces) {
        if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) || !std::isfinite(p.pos.z)) return false;
        if (std::abs(p.pos.x) > 4 || std::abs(p.pos.z) > 4 || p.pos.y < -0.5f || p.pos.y > 3.5f) return false;
    }
    return true;
}

static void TestWaitingAreaLight(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/waiting_area.json"));
    const int lamp = FindByName(w, "CeilingLamp");
    const int sw = FindByName(w, "WallSwitch");
    CHECK(lamp > 0 && sw > 0);
    w.Advance(0.5);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);   // starts off
    w.DebugActuate(sw, true);                            // push the rocker
    w.Advance(0.6);
    CHECK(w.PartOf(lamp)->brightness > 0.9);
    w.Advance(2.0);                                         // stays on by itself (bistable)
    CHECK(w.PartOf(lamp)->brightness > 0.9);
    w.DebugActuate(sw, false);
    w.Advance(0.6);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
    CHECK(FrameSane(w));
}

static void TestTestZoneButton(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    const int lamp = FindByName(w, "BulbOnStand");
    const int room = FindByName(w, "RoomLamp");
    const int button = FindByName(w, "BigButton");
    w.Advance(1.5);                                         // let everything settle
    CHECK(w.PartOf(room)->brightness > 0.9);             // room switch starts closed
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
    CHECK(w.ClipAttachedEntity(FindByName(w, "Wire_Red"), 0) == FindByName(w, "Battery_9V"));
    w.DebugActuate(button, true);                        // press the plunger down
    w.Advance(0.3);
    CHECK(w.PartOf(lamp)->brightness > 0.5);
    w.DebugActuate(button, false);                       // spring returns it
    w.Advance(0.3);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
    w.Advance(3.0);
    CHECK(FrameSane(w));
}

static void TestLooseBatterySettles(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    const int spare = FindByName(w, "SpareBattery_9V");
    w.Advance(2.0);
    const Vec3 p = w.BodyPosition(w.MainBodyOf(spare));
    CHECK_NEAR(p.y, 0.74 + 0.024, 0.01);                 // standing on the table top
}

static Zone TerminalBench() {
    Zone z;
    z.name = "Bench";
    Entity floor; floor.id = 1; floor.name = "Floor"; floor.pos = {0, -0.05f, 0}; floor.size = {4, 0.1f, 4};
    Entity post; post.id = 2; post.name = "Post"; post.type = "resistor"; post.pos = {0, 0.05f, 0}; post.size = {0.1f, 0.1f, 0.1f};
    post.params["resistance"] = 10.0;
    Entity wire; wire.id = 3; wire.name = "Lead"; wire.type = "wire"; wire.pos = {0.3f, 0.01f, 0}; wire.size = {0.4f, 0.005f, 0.005f};
    wire.massKg = 0.01f; wire.params["length"] = 0.4;
    z.entities = {floor, post, wire};
    z.links = {{3, 0, 2, 1}};                            // lead end 0 clipped onto post terminal B
    return z;
}

static void TestClipPullOffAndReattach() {
    World w;
    w.SetZone(TerminalBench());
    w.Advance(1.0);
    CHECK(w.ClipAttachedEntity(3, 0) == 2);

    const int clip = w.ClipBodyOf(3, 0);
    const Vec3 start = w.BodyPosition(clip);
    w.BeginGrab(clip, start);
    w.SetGrabTarget(start + Vec3{0.25f, 0.3f, 0});        // yank it away
    w.Advance(1.0);
    CHECK(w.ClipAttachedEntity(3, 0) == 0);             // pulled off (post is fixed)

    const Vec3 term = w.TerminalPositions(2)[1];
    w.SetGrabTarget(term);                               // bring it back and let go
    w.Advance(1.5);
    w.EndGrab();
    w.Advance(0.5);
    CHECK(w.ClipAttachedEntity(3, 0) == 2);             // latched again
    CHECK(FrameSane(w));
}

static void TestDirectTouchLightsBulb() {
    Zone z;
    Entity bat; bat.id = 1; bat.name = "Bat"; bat.type = "battery"; bat.pos = {0, 1, 0}; bat.size = {0.026f, 0.048f, 0.017f};
    bat.params = {{"voltage", 9.0}, {"internalR", 1.5}, {"capacityAh", 0.5}};
    Entity bulb; bulb.id = 2; bulb.name = "Bulb"; bulb.type = "lamp"; bulb.size = {0.013f, 0.04f, 0.013f};
    bulb.params = {{"ratedPower", 1.0}, {"ratedVoltage", 9.0}};
    bulb.pos = {0, 1 + 0.024f + 0.7f * 0.02f, 0};       // bulb contacts resting on battery terminals
    z.entities = {bat, bulb};

    World w;
    w.SetZone(z);
    w.Advance(0.1);
    CHECK(w.PartOf(2)->brightness > 0.5);

    z.entities[1].pos.y += 0.02f;                        // lift it 2 cm: no contact
    w.SetZone(z);
    w.Advance(0.1);
    CHECK_NEAR(w.PartOf(2)->brightness, 0.0, 1e-6);
}

static void TestHingeAndSliderBasics() {
    Physics ph;
    BodyDesc base; base.halfExtent = {0.05f, 0.05f, 0.05f};
    const BodyHandle b = ph.CreateBody(base);
    BodyDesc lev; lev.pos = {0, 0.06f, 0}; lev.halfExtent = {0.02f, 0.005f, 0.01f}; lev.mass = 0.01f;
    const BodyHandle l = ph.CreateBody(lev);
    const Quat q = Quat::AxisAngle({0, 0, 1}, 0.2f);
    const ConstraintHandle h = ph.AddHinge(b, l, {0, 0.05f, 0}, {0, 0, 1}, {0, 1, 0}, q.Rotate({0, 1, 0}), -0.3f, 0.3f, 0.f);
    CHECK_NEAR(ph.HingeAngle(h), 0.2, 0.02);            // initial angle follows the normals
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
    TestHingeAndSliderBasics();
    TestWaitingAreaLight(content);
    TestTestZoneButton(content);
    TestLooseBatterySettles(content);
    TestClipPullOffAndReattach();
    TestDirectTouchLightsBulb();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
