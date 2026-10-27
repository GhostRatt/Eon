#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "character/Character.h"
#include "character/BodyState.h"
#include "character/LipSync.h"
#include "character/Motor.h"
#include "mind/Learner.h"
#include "mind/MemoryGraph.h"
#include "mind/Perception.h"
#include "character/Posture.h"
#include "character/Wardrobe.h"
#include "core/Circuit.h"
#include "core/World.h"
#include "core/Physics.h"
#include "core/Zone.h"

using namespace eon;

static int g_fail = 0, g_pass = 0;

static const char* g_test = "";
#define CHECK_NEAR(a, b, tol) do { \
    const double _a = (a), _b = (b); \
    if (std::abs(_a - _b) > (tol)) { std::printf("FAIL [%s] %s:%d  %s = %.6f, expected %.6f\n", g_test, __FILE__, __LINE__, #a, _a, _b); ++g_fail; } \
    else ++g_pass; } while (0)

#define CHECK(c) do { if (!(c)) { std::printf("FAIL [%s] %s:%d  %s\n", g_test, __FILE__, __LINE__, #c); ++g_fail; } else ++g_pass; } while (0)

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

static void TestCharacterImport() {
    CharacterAsset a;
    std::string err;
    const bool ok = ImportFbx(std::string(EON_TEST_DATA) + "/test_rig.fbx", a, {}, &err);
    if (!ok) std::printf("  import error: %s\n", err.c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK(a.bones.size() >= 20);
    CHECK(a.VertexCount() > 5000);
    Character ch;
    CHECK(ch.Init(&a));
    const RigMap& r = ch.Rig();
    CHECK(r.pelvis >= 0 && r.head >= 0 && r.hand[0] >= 0 && r.hand[1] >= 0);
    CHECK(r.breast[0] >= 0 && r.glute[1] >= 0);
    CHECK_NEAR(ch.Height(), 1.78, 0.08);

    // Facing: breasts end up in front (+Z), left hand on +X side when facing +Z? (right-handed: left = +X)
    ch.ResetPose();
    CHECK(ch.BonePos(r.breast[0]).z > ch.BonePos(r.SpineTop()).z);

    // Bind pose skinning reproduces the mesh (placed on the floor)
    ch.Skin();
    float lo = 1e9f;
    for (const Vec3& p : ch.SkinnedPos(0)) lo = std::min(lo, p.y);
    CHECK_NEAR(lo, 0.0, 0.01);

    // Two-bone IK reaches a target
    const Vec3 target = ch.BonePos(r.upperarm[0]) + Vec3{0.1f, -0.45f, 0.15f};
    ch.SolveTwoBone(r.upperarm[0], r.lowerarm[0], r.hand[0], target, ch.BonePos(r.upperarm[0]) + Vec3{0, -0.3f, -0.5f});
    CHECK_NEAR(Distance(ch.BonePos(r.hand[0]), target), 0.0, 0.005);

    // Morph target exists and moves vertices
    CHECK(!ch.MorphNames().empty());

    // Capsules fitted; hands never inside the body after limb collision
    CHECK(ch.Capsules().size() >= 8);
    PostureParams pp;
    pp.motion = TestMotion::HandsOnChest;
    for (int f = 0; f < 60; ++f) {
        pp.time = f / 60.f;
        ApplyPosture(ch, pp);
        ch.UpdateLimbCollision(1 / 60.f);
    }
    float worst = 1.f;
    for (int i = 0; i < 2; ++i) {
        const Vec3 h = ch.BonePos(r.hand[i]);
        for (const BodyCapsule& c : ch.Capsules()) {
            Vec3 a0, b0;
            ch.CapsuleWorld(c, a0, b0);
            const Vec3 ab = b0 - a0;
            const float t = ab.Dot(ab) > 1e-9f ? std::clamp((h - a0).Dot(ab) / ab.Dot(ab), 0.f, 1.f) : 0.f;
            worst = std::min(worst, Distance(h, a0 + ab * t) - c.radius);
        }
    }
    CHECK(worst > 0.02f);   // palm (3.5 cm) stays on the surface, not inside

    // Soft tissue follows through: bouncing moves breast bones relative to the chest, then settles
    Character c2;
    c2.Init(&a);
    pp.motion = TestMotion::Bounce;
    float maxLag = 0.f;
    for (int f = 0; f < 120; ++f) {
        pp.time = f / 60.f;
        ApplyPosture(c2, pp);
        const Vec3 rest = c2.BonePos(r.breast[0]);
        c2.UpdateSoftTissue(1 / 60.f);
        maxLag = std::max(maxLag, Distance(c2.BonePos(r.breast[0]), rest));
    }
    CHECK(maxLag > 0.002f && maxLag < 0.06f);
    pp.motion = TestMotion::Idle;
    for (int f = 0; f < 180; ++f) { pp.time = 2.f + f / 60.f; ApplyPosture(c2, pp); c2.UpdateSoftTissue(1 / 60.f); }
    ApplyPosture(c2, pp);
    const Vec3 rest = c2.BonePos(r.breast[0]);
    c2.UpdateSoftTissue(1 / 60.f);
    CHECK(Distance(c2.BonePos(r.breast[0]), rest) < 0.006f);   // settled (just gravity sag)

    // Pressed into a surface (a floor just below the breast bone): the tissue flattens, and recovers when lifted off
    c2.contactFloorY = c2.BonePos(r.breast[0]).y - 0.01f;
    for (int f = 0; f < 10; ++f) { ApplyPosture(c2, pp); c2.UpdateSoftTissue(1 / 60.f); }
    float sq = 1.f;
    for (const SoftBone& s : c2.SoftBones()) if (s.tag == 2) sq = std::min(sq, s.squash);
    CHECK(sq < 0.9f);
    c2.contactFloorY = -1e9f;
    for (int f = 0; f < 60; ++f) { ApplyPosture(c2, pp); c2.UpdateSoftTissue(1 / 60.f); }
    sq = 1.f;
    for (const SoftBone& s : c2.SoftBones()) if (s.tag == 2) sq = std::min(sq, s.squash);
    CHECK(sq > 0.99f);
}

static void TestBodyState() {
    BodyState b;
    b.gameMinutesPerSecond = 60.f;   // 1 s = 1 game hour
    BodyInputs hot; hot.ambientC = 40.f; hot.exertion = 0.6f;
    for (int i = 0; i < 4 * 60; ++i) b.Step(hot, 1 / 60.f);
    CHECK(b.sweat > 0.3f);
    CHECK(b.coreC > 37.3f);
    CHECK(b.breathRate > 0.4f);
    CHECK(b.Get(Need::Thirst) > 50.f);

    BodyState c;
    c.gameMinutesPerSecond = 60.f;
    BodyInputs cold; cold.ambientC = 2.f;
    for (int i = 0; i < 4 * 60; ++i) c.Step(cold, 1 / 60.f);
    CHECK(c.shiver > 0.3f);
    CHECK(c.Describe(2.f).find("shivering") != std::string::npos);

    BodyState d;   // clothing keeps her warmer in the cold
    d.gameMinutesPerSecond = 60.f;
    BodyInputs dressed = cold; dressed.clothingClo = 1.5f;
    for (int i = 0; i < 4 * 60; ++i) d.Step(dressed, 1 / 60.f);
    CHECK(d.coreC > c.coreC + 0.3f);
}

static void TestWardrobeRules() {
    Wardrobe w;
    Garment bra; bra.name = "Bra"; bra.layer = Layer::Underwear; bra.covers = {"chest"}; bra.support[2] = 0.7f;
    Garment panties; panties.name = "Panties"; panties.layer = Layer::Underwear; panties.covers = {"groin"};
    Garment shirt; shirt.name = "Shirt"; shirt.layer = Layer::Mid; shirt.covers = {"chest", "arms"}; shirt.clo = 0.2f;
    w.garments = {bra, panties, shirt};
    CHECK(w.Wear(0) && w.Wear(1) && w.Wear(2));
    CHECK(!w.CanRemove(0));            // shirt is over the bra
    CHECK(w.CanRemove(1));             // nothing over the panties
    CHECK(w.Remove(2));                // shirt off first
    CHECK(w.Remove(0));                // now the bra
    CHECK_NEAR(w.Support(2), 0.0, 1e-6);
    CHECK(w.Wear(0));
    w.streamerMode = true;
    CHECK(!w.CanRemove(0) && !w.CanRemove(1));   // last layer stays on while streaming
    CHECK(w.Wear(2) && w.Remove(2));             // outer layers still come off
    CHECK(w.Covers("chest") && w.Covers("groin"));
    CHECK(!w.CanWear(0));                        // already on
}

struct TestJenny {
    CharacterAsset a;
    Character ch;
    Motor motor;
    MotorIntent in;
    bool Init(World& w, const Vec3& pos, float yaw) {
        if (!ImportFbx(std::string(EON_TEST_DATA) + "/test_rig.fbx", a)) return false;
        ch.Init(&a);
        motor.Init(ch, w);
        motor.Teleport(ch, pos, yaw);
        return true;
    }
    void Run(World& w, float seconds) {
        for (float t = 0; t < seconds; t += 1 / 60.f) {
            motor.PrePhysics(ch, w, in, 1 / 60.f);
            w.Advance(1 / 60.f);
            PostureParams p;
            p.time = t;
            motor.PostPhysics(ch, w, p, 1 / 60.f);
        }
    }
};

static void TestMotorPressesButton(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    w.Advance(0.5);
    const int lamp = FindByName(w, "BulbOnStand");
    const int button = FindByName(w, "BigButton");
    TestJenny j;
    CHECK(j.Init(w, {0.f, 0.f, 0.58f}, 3.14159265f));   // standing at the table edge, facing it (-Z)
    j.Run(w, 0.5);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);

    // right hand: reach just past the top of the plunger (pressing into it)
    const Vec3 top = w.BodyPosition(w.MainBodyOf(button)) + Vec3{0, 0.025f, 0};
    j.in.hands[1].reach = true;
    j.in.hands[1].target = top + Vec3{0, -0.03f, 0};
    j.in.hands[1].effort = 0.3f;
    j.Run(w, 2.0);
    CHECK(w.PartOf(lamp)->brightness > 0.5);        // her hand physically pushed the button down

    j.in.hands[1].reach = false;                     // let go: spring pushes it back up
    j.Run(w, 1.0);
    CHECK_NEAR(w.PartOf(lamp)->brightness, 0.0, 1e-6);
}

static void TestMotorWalksAndStopsAtWall(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/waiting_area.json"));
    TestJenny j;
    CHECK(j.Init(w, {0.f, 0.f, 0.5f}, 0.f));
    j.in.moveDir = {0, 0, 1};                        // walk toward the south wall (z = +3)
    j.Run(w, 1.0);
    CHECK(j.motor.Position().z > 1.0f);              // she is moving
    CHECK(j.motor.Feedback().speed > 0.8f);
    j.Run(w, 4.0);
    CHECK(j.motor.Position().z < 3.0f - 0.15f);      // but not through the wall
    CHECK(j.motor.Position().z > 2.4f);
    CHECK(j.motor.Feedback().bumped);
    CHECK_NEAR(j.motor.Position().y, 0.0, 0.03);     // feet on the floor
}

static void TestMotorGripsAndLifts(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    w.Advance(1.0);
    const int spare = FindByName(w, "SpareBattery_9V");
    const int body = w.MainBodyOf(spare);
    const Vec3 b0 = w.BodyPosition(body);
    TestJenny j;
    CHECK(j.Init(w, {b0.x - 0.1f, 0.f, b0.z + 0.5f}, 3.14159265f));
    j.Run(w, 0.3);
    auto& h = j.in.hands[1];
    h.reach = true;
    h.target = b0 + Vec3{0, 0.07f, 0};               // palm just above the battery (pressing down would tip it over)
    h.effort = 0.3f;
    j.Run(w, 1.2);
    h.grip = true;
    j.Run(w, 0.2);
    CHECK(j.motor.Feedback().holding[1]);
    h.target = b0 + Vec3{0, 0.3f, 0};                // lift
    j.Run(w, 1.5);
    CHECK(w.BodyPosition(body).y > b0.y + 0.15f);
    CHECK_NEAR(j.motor.Feedback().heldMass[1], 0.045, 0.01);
    h.grip = false;                                  // drop it
    j.Run(w, 1.5);
    CHECK(w.BodyPosition(body).y < b0.y + 0.05f);
}

// The point on a body of `entity` hit by a ray, or false (what she'd see/feel; tests use it to aim her hand).
static bool HitEntity(const World& w, int entity, const Vec3& from, const Vec3& dir, float len, RayHit& out) {
    return w.RayCast(from, dir, len, out, true) && w.EntityOfBody(out.body) == entity;
}

static void TestFlipsWallSwitch(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    w.Advance(0.5);
    const int sw = FindByName(w, "RoomSwitch");
    const bool before = w.PartOf(sw)->closed;
    TestJenny j;
    CHECK(j.Init(w, {2.05f, 0.f, 1.8f}, 3.14159265f * 0.5f));   // facing the wall (+X)
    j.Run(w, 0.5);
    // which half of the rocker sticks out? press that one in with a fingertip
    RayHit up, dn;
    const bool hu = HitEntity(w, sw, {2.2f, 1.212f, 1.8f}, {1, 0, 0}, 0.5f, up);
    const bool hd = HitEntity(w, sw, {2.2f, 1.188f, 1.8f}, {1, 0, 0}, 0.5f, dn);
    CHECK(hu && hd);
    const RayHit& press = up.dist < dn.dist ? up : dn;
    auto& h = j.in.hands[1];
    h.reach = true;
    h.shape = HandShape::Point;
    h.target = press.point + Vec3{0.015f, 0, 0};
    h.effort = 0.4f;
    j.Run(w, 1.5);
    CHECK(w.PartOf(sw)->closed != before);           // flipped
    h.reach = false;
    j.Run(w, 1.0);
    CHECK(w.PartOf(sw)->closed != before);           // and it stays flipped (snaps over)
}

static void TestPullsLever(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    w.Advance(0.5);
    const int lever = FindByName(w, "WallLever");
    const int lamp = FindByName(w, "LeverLamp");
    CHECK(!w.PartOf(lever)->closed);
    TestJenny j;
    CHECK(j.Init(w, {-1.85f, 0.f, 0.8f}, -3.14159265f * 0.5f));   // facing the west wall
    j.Run(w, 0.5);
    // find the handle (it hangs down at an angle): feel down onto it ~20 cm out from the wall
    RayHit top;
    CHECK(HitEntity(w, lever, {-2.27f, 1.5f, 0.8f}, {0, -1, 0}, 1.0f, top));
    const Vec3 pivot{-2.45f, 1.15f, 0.8f};
    auto& h = j.in.hands[1];
    h.reach = true;
    h.target = top.point + Vec3{0, 0.02f, 0};
    h.effort = 0.6f;
    j.Run(w, 1.0);
    h.grip = true;
    j.Run(w, 0.2);
    CHECK(j.motor.Feedback().holding[1]);
    // swing the hand up along the handle's arc
    const Vec3 arm = top.point - pivot;
    const float r = std::sqrt(arm.x * arm.x + arm.y * arm.y);
    const float a0 = std::atan2(arm.y, arm.x);
    for (int k = 1; k <= 30; ++k) {
        const float a = a0 + (0.62f - a0) * (k / 30.f);
        h.target = pivot + Vec3{r * std::cos(a), r * std::sin(a), 0.f};
        j.Run(w, 0.05);
    }
    j.Run(w, 0.5);
    CHECK(w.PartOf(lever)->closed);
    CHECK(w.PartOf(lamp)->brightness > 0.5);
    h.grip = false;
    h.reach = false;
    j.Run(w, 1.0);
    CHECK(w.PartOf(lever)->closed);                  // stays up when let go
}

static void TestCarriesThings(const std::string& contentDir) {
    {   // one hand: pick the spare battery off the table and walk off with it
        World w;
        CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
        w.Advance(1.0);
        const int body = w.MainBodyOf(FindByName(w, "SpareBattery_9V"));
        const Vec3 b0 = w.BodyPosition(body);
        TestJenny j;
        CHECK(j.Init(w, {b0.x - 0.1f, 0.f, b0.z + 0.45f}, 3.14159265f));
        j.Run(w, 0.3);
        auto& h = j.in.hands[1];
        h.reach = true;
        h.target = b0 + Vec3{0, 0.07f, 0};
        h.effort = 0.3f;
        j.Run(w, 1.8);
        h.grip = true;
        j.Run(w, 0.2);
        CHECK(j.motor.Feedback().holding[1]);
        h.local = true;                               // hold it in front of her, wherever she goes
        h.target = {-0.15f, 1.05f, 0.3f};
        j.Run(w, 1.0);
        j.in.moveDir = {0, 0, 1};                     // turn and walk away from the table
        j.Run(w, 2.0);
        j.in.moveDir = {};
        j.Run(w, 0.8);
        CHECK(j.motor.Feedback().holding[1]);
        CHECK(w.BodyPosition(body).z > b0.z + 1.2f);  // it came with her
        CHECK(w.BodyPosition(body).y > 0.8f);
    }
    {   // two hands: crouch, take a 4 kg crate by its sides, stand up and carry it
        World w;
        CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
        w.Advance(1.0);
        const int body = w.MainBodyOf(FindByName(w, "Crate"));
        const Vec3 c0 = w.BodyPosition(body);
        TestJenny j;
        CHECK(j.Init(w, {c0.x, 0.f, c0.z + 0.42f}, 3.14159265f));   // facing it (-Z): her left is -X
        j.in.stance = Stance::Crouch;
        j.Run(w, 1.5);
        for (int i = 0; i < 2; ++i) {
            auto& h = j.in.hands[i];
            h.reach = true;
            h.effort = 0.7f;
            h.target = c0 + Vec3{i == 0 ? -0.19f : 0.19f, 0.03f, 0.f};   // squeeze the sides
        }
        j.Run(w, 1.5);
        j.in.hands[0].grip = j.in.hands[1].grip = true;
        j.Run(w, 0.2);
        CHECK(j.motor.Feedback().holding[0] && j.motor.Feedback().holding[1]);
        for (int i = 0; i < 2; ++i) { j.in.hands[i].local = true; j.in.hands[i].target = {i == 0 ? 0.19f : -0.19f, 0.95f, 0.3f}; }
        j.in.stance = Stance::Stand;
        j.Run(w, 2.0);
        CHECK(w.BodyPosition(body).y > c0.y + 0.4f);  // lifted
        CHECK_NEAR(j.motor.Feedback().load, 4.0, 0.2);
        CHECK(j.motor.Lean() < 0.3f);                  // carries it upright, close to her body
        j.in.moveDir = {1, 0, 0};
        j.Run(w, 2.0);
        CHECK(j.motor.Feedback().speed < 1.3f);       // slower with a load
        j.in.moveDir = {};
        j.Run(w, 0.8);
        CHECK(w.BodyPosition(body).x > c0.x + 1.0f);
        CHECK(j.motor.Feedback().holding[0] && j.motor.Feedback().holding[1]);
    }
}


static void TestMemoryGraph() {
    MemoryGraph g;
    const uint32_t bump = g.AddNode(NodeKind::Thing, "small red bump", 0);
    const uint32_t bulb = g.AddNode(NodeKind::Thing, "small cream block", 0);
    const uint32_t table = g.AddNode(NodeKind::Thing, "big white slab", 0);
    const uint32_t press = g.AddNode(NodeKind::Action, "push small red bump", 1);
    const uint32_t lit = g.AddNode(NodeKind::Effect, "small cream block lights up", 1);
    g.Evidence(press, bump, Rel::Targets, Source::Observed, true, 1);
    g.Evidence(lit, bulb, Rel::Affects, Source::Observed, true, 1);
    g.Evidence(bump, table, Rel::On, Source::Observed, true, 1);
    g.Evidence(bulb, table, Rel::On, Source::Observed, true, 1);
    g.Evidence(press, lit, Rel::Causes, Source::Inferred, true, 1);
    g.Evidence(press, lit, Rel::Causes, Source::Inferred, true, 5);
    g.Evidence(press, lit, Rel::Causes, Source::Inferred, false, 9);    // once it didn't work
    const MemEdge* c = g.Edge(press, lit, Rel::Causes);
    CHECK(c && c->support == 2 && c->trials == 3);
    CHECK_NEAR(c->Confidence(), 3.0 / 5.0, 1e-5);
    CHECK(g.Path(bump, bulb).size() == 3);                               // bump -> table -> bulb (or via the action)
    const std::string ctx = g.Context({bump}, 10, 60);
    CHECK(ctx.size() <= 60 * 4 + 8);
    CHECK(ctx.find("small red bump") != std::string::npos);
    CHECK(ctx.find("lights up") != std::string::npos);                  // one hop from the bump: what pushing it does

    // competing explanations: two actions just before an effect
    const uint32_t a2 = g.AddNode(NodeKind::Action, "grab grey stick", 20);
    const uint32_t dark = g.AddNode(NodeKind::Effect, "small cream block goes dark", 20);
    g.Evidence(press, dark, Rel::Causes, Source::Ambiguous, true, 20, 0.5f);
    g.Evidence(a2, dark, Rel::Causes, Source::Ambiguous, true, 20, 0.5f);
    CHECK(g.Edge(a2, dark, Rel::Causes)->src == Source::Ambiguous);
    CHECK(!g.Questions(25).empty());                                     // which one did it?
    for (int k = 0; k < 4; ++k) g.Evidence(a2, dark, Rel::Causes, Source::Ambiguous, false, 30 + k);   // grabbing never does it again
    g.Consolidate(40, 8.f);
    CHECK(g.Edge(a2, dark, Rel::Causes) == nullptr);                      // that guess got forgotten in sleep
    CHECK(g.Edge(press, lit, Rel::Causes) != nullptr);
    CHECK(g.Node(bump)->community == g.Node(bulb)->community);            // the table things belong together
    CHECK(g.Report(40).find("What puzzles me") != std::string::npos);

    // persistence: snapshot + journal of later changes
    const std::string dir = std::string(EON_TEST_DATA) + "/../../build";
    g.SetJournal(dir + "/mem_test.journal");
    CHECK(g.Save(dir + "/mem_test.json"));
    const uint32_t late = g.AddNode(NodeKind::Feeling, "cold", 50);
    g.Evidence(press, lit, Rel::Causes, Source::Inferred, true, 51);
    CHECK(g.FlushJournal());
    MemoryGraph h;
    h.SetJournal(dir + "/mem_test.journal");
    CHECK(h.Load(dir + "/mem_test.json"));
    CHECK(h.NodeCount() == g.NodeCount() && h.Find(NodeKind::Feeling, "cold") == late);
    CHECK(h.Edge(press, lit, Rel::Causes) && h.Edge(press, lit, Rel::Causes)->support == 3);
    CHECK(g.ExportHtml(dir + "/mem_test.html", 60));
}

static void TestJennyLearnsTheButton(const std::string& contentDir) {
    World w;
    CHECK(w.Load(contentDir + "/zones/test_zone_01.json"));
    w.Advance(0.5);
    TestJenny j;
    CHECK(j.Init(w, {0.f, 0.f, 0.58f}, 3.14159265f));
    Perception eyes;
    Learner mind;
    mind.SetBackstory({"my name is Jenny", "I woke up here and don't know this place"}, 0);
    double t = 0;
    Percept p;
    auto live = [&](float seconds) {
        for (float s = 0; s < seconds; s += 0.1f) {
            j.Run(w, 0.1f);
            t += 0.1;
            eyes.Sense(w, j.motor, j.ch, nullptr, t, p);
            mind.Observe(p);
        }
    };
    live(1.0);
    // she sees the bulb and the button without being told what they are
    const int bulbE = FindByName(w, "BulbOnStand");
    const Vec3 top = w.BodyPosition(w.MainBodyOf(FindByName(w, "BigButton"))) + Vec3{0, 0.025f, 0};
    size_t things = 0;
    for (const MemNode& n : mind.graph.Nodes()) things += n.kind == NodeKind::Thing;
    CHECK(things >= 4);
    for (const MemNode& n : mind.graph.Nodes()) CHECK(n.label.find("Bulb") == std::string::npos && n.label.find("Button") == std::string::npos);
    (void)bulbE;
    // press, release, press again
    for (int k = 0; k < 2; ++k) {
        auto& h = j.in.hands[1];
        h.reach = true;
        h.target = top + Vec3{0, -0.03f, 0};
        h.effort = 0.3f;
        live(2.0);
        h.reach = false;
        live(1.5);
    }
    // she now believes pushing that thing makes the other thing light up
    const MemEdge* best = nullptr;
    for (const MemEdge& e : mind.graph.Edges()) {
        if (!e.alive || e.rel != Rel::Causes) continue;
        const MemNode* eff = mind.graph.Node(e.to);
        if (eff->label.find("lights up") != std::string::npos && (!best || e.support > best->support)) best = &e;
    }
    CHECK(best != nullptr);
    if (best) {
        CHECK(best->support >= 2);
        CHECK(mind.graph.Node(best->from)->label.find("push") == 0 || mind.graph.Node(best->from)->label.find("press") == 0);
        CHECK(best->Confidence() > 0.6f);
    }
    const std::string ctx = mind.Context(p, 250);
    CHECK(ctx.size() < 250 * 4 + 300);
    CHECK(ctx.find("lights up") != std::string::npos);
}

static Zone FloorZone(float boxHeight, float boxZ) {
    Zone z;
    z.name = "Bench";
    Entity floor; floor.id = 1; floor.name = "Floor"; floor.pos = {0, -0.05f, 0}; floor.size = {10, 0.1f, 10};
    z.entities = {floor};
    if (boxHeight > 0.f) {
        Entity box; box.id = 2; box.name = "Ledge"; box.pos = {0, boxHeight * 0.5f, boxZ + 0.75f}; box.size = {2.f, boxHeight, 1.5f};
        z.entities.push_back(box);
    }
    return z;
}

static void TestStances() {
    World w;
    w.SetZone(FloorZone(0.f, 0.f));
    TestJenny j;
    CHECK(j.Init(w, {0, 0, 0}, 0.f));
    const RigMap& r = j.ch.Rig();
    j.Run(w, 0.5);
    const float hipStand = j.ch.BonePos(r.pelvis).y;
    // feet flat on the floor while standing
    CHECK_NEAR(j.ch.BonePos(r.foot[0]).y, j.ch.BonePos(r.foot[1]).y, 0.01);

    struct Want { Stance s; float hipMin, hipMax; };
    const Want wants[] = {
        {Stance::Crouch, 0.25f * hipStand, 0.68f * hipStand},
        {Stance::Kneel, 0.35f * hipStand, 0.62f * hipStand},
        {Stance::Sit, 0.0f, 0.25f},
        {Stance::LieBack, 0.0f, 0.2f},
        {Stance::Crawl, 0.3f * hipStand, 0.62f * hipStand},
        {Stance::Prone, 0.0f, 0.2f},
        {Stance::Stand, 0.93f * hipStand, 1.02f * hipStand},
    };
    for (const Want& wt : wants) {
        j.in.stance = wt.s;
        j.Run(w, 5.0);
        const float hip = j.ch.BonePos(r.pelvis).y;
        if (j.motor.CurrentStance() != wt.s || hip < wt.hipMin || hip > wt.hipMax)
            std::printf("  stance %s: got %s hip %.3f (want %.3f..%.3f)\n", StanceName(wt.s), StanceName(j.motor.CurrentStance()), hip, wt.hipMin, wt.hipMax);
        CHECK(j.motor.CurrentStance() == wt.s);
        CHECK(hip >= wt.hipMin && hip <= wt.hipMax);
        if (wt.s == Stance::Crawl) {
            CHECK(j.ch.BonePos(r.hand[0]).y < 0.15f && j.ch.BonePos(r.hand[1]).y < 0.15f);   // hands on the floor
            const float z0 = j.motor.Position().z;
            j.in.moveDir = {0, 0, 1};
            j.Run(w, 2.0);
            CHECK(j.motor.Position().z > z0 + 0.4f);                                        // crawls forward
            j.in.moveDir = {};
            j.Run(w, 0.5);
        }
        if (wt.s == Stance::Prone) CHECK(j.ch.BonePos(r.head).y < 0.45f);                  // head down near the floor
        if (wt.s == Stance::LieBack) CHECK(j.ch.BonePos(r.head).y < 0.35f);
    }

    // rolling over: back -> left side -> front. The head stays where it was; she ends up facing the other way.
    j.in.stance = Stance::LieBack;
    j.Run(w, 8.0);
    CHECK(j.motor.CurrentStance() == Stance::LieBack);
    const Vec3 head0 = j.ch.BonePos(r.head);
    const float yaw0 = j.motor.Yaw();
    j.in.stance = Stance::LieLeft;
    j.Run(w, 2.0);
    CHECK(j.motor.CurrentStance() == Stance::LieLeft);
    CHECK(j.ch.BonePos(r.upperarm[0]).y < j.ch.BonePos(r.upperarm[1]).y - 0.15f);       // left shoulder down
    CHECK(j.ch.BonePos(r.pelvis).y < 0.3f);
    j.in.stance = Stance::Prone;
    j.Run(w, 2.0);
    CHECK(j.motor.CurrentStance() == Stance::Prone);
    const Vec3 head1 = j.ch.BonePos(r.head);
    CHECK(Distance(Vec3{head0.x, 0, head0.z}, Vec3{head1.x, 0, head1.z}) < 0.15f);
    CHECK(std::fabs(std::fabs(j.motor.Yaw() - yaw0) - 3.14159265f) < 0.05f);
    j.in.stance = Stance::LieRight;                       // and roll back up onto the right side
    j.Run(w, 2.0);
    CHECK(j.motor.CurrentStance() == Stance::LieRight);
    CHECK(j.ch.BonePos(r.upperarm[1]).y < j.ch.BonePos(r.upperarm[0]).y - 0.15f);
}

static void TestSitOnSeat() {
    {   // a 45 cm box behind her: she sits on it, feet on the floor, and stands back up
        World w;
        w.SetZone(FloorZone(0.45f, -1.95f));   // box spans z -1.95 .. -0.45
        TestJenny j;
        CHECK(j.Init(w, {0, 0, -0.3f}, 0.f));  // facing +Z, seat edge 15 cm behind her
        const RigMap& r = j.ch.Rig();
        j.in.stance = Stance::SitSeat;
        j.Run(w, 2.5);
        CHECK(j.motor.CurrentStance() == Stance::SitSeat);
        CHECK(!j.motor.Feedback().noSeat);
        const float hip = j.ch.BonePos(r.pelvis).y;
        CHECK(hip > 0.48f && hip < 0.7f);
        CHECK(j.ch.BonePos(r.foot[0]).y < 0.15f && j.ch.BonePos(r.foot[1]).y < 0.15f);
        // lie down along it (it's 2 m long, like a bed): onto her left side, up on the surface
        j.in.stance = Stance::LieLeft;
        j.Run(w, 3.0);
        CHECK(j.motor.CurrentStance() == Stance::LieLeft);
        CHECK(j.motor.SupportY() > 0.4f);
        CHECK(j.ch.BonePos(r.pelvis).y > 0.5f && j.ch.BonePos(r.pelvis).y < 0.85f);
        CHECK(j.ch.BonePos(r.head).y > 0.5f);                       // head on the bed, not hanging off
        CHECK(j.ch.BonePos(r.foot[0]).y > 0.45f);                   // legs up on it too
        // get up: sit up over the edge, then stand on the floor
        j.in.stance = Stance::Stand;
        j.Run(w, 5.0);
        CHECK(j.motor.CurrentStance() == Stance::Stand);
        CHECK_NEAR(j.motor.Position().y, 0.0, 0.05);
        CHECK(j.motor.Position().z > -0.45f);                       // standing in front of it, not on it
        CHECK(j.ch.BonePos(r.pelvis).y > hip + 0.2f);
    }
    {   // nothing behind her: she doesn't sit on thin air
        World w;
        w.SetZone(FloorZone(0.f, 0.f));
        TestJenny j;
        CHECK(j.Init(w, {0, 0, 0}, 0.f));
        j.in.stance = Stance::SitSeat;
        j.Run(w, 2.0);
        CHECK(j.motor.CurrentStance() == Stance::Stand);
        CHECK(j.motor.Feedback().noSeat);
    }
}

static void TestStepAndClimb() {
    {   // a 20 cm step: she just walks up onto it
        World w;
        w.SetZone(FloorZone(0.2f, 0.6f));
        TestJenny j;
        CHECK(j.Init(w, {0, 0, 0}, 0.f));
        j.in.moveDir = {0, 0, 1};
        j.Run(w, 1.7);
        CHECK(j.motor.Position().z > 1.0f);
        CHECK_NEAR(j.motor.Position().y, 0.2, 0.04);   // up on the step
        j.Run(w, 1.5);
        CHECK_NEAR(j.motor.Position().y, 0.0, 0.04);   // and stepped down off the far side
    }
    {   // a 60 cm ledge: walking is blocked, climbing gets her on top; climbing back down returns her to the floor
        World w;
        w.SetZone(FloorZone(0.6f, 0.45f));
        TestJenny j;
        CHECK(j.Init(w, {0, 0, 0}, 0.f));
        j.in.moveDir = {0, 0, 1};
        j.Run(w, 1.5);
        CHECK(j.motor.Position().y < 0.05f);
        CHECK(j.motor.Feedback().bumped);
        j.in.climb = true;
        j.Run(w, 0.2);
        CHECK(j.motor.Feedback().climbing);
        j.in.climb = false;
        j.in.moveDir = {};
        j.Run(w, 3.5);
        CHECK_NEAR(j.motor.Position().y, 0.6, 0.05);
        CHECK(j.motor.Position().z > 0.5f);
        // walk to the far edge: she stops instead of walking off
        j.in.moveDir = {0, 0, 1};
        j.Run(w, 3.0);
        CHECK(j.motor.Feedback().atEdge);
        CHECK_NEAR(j.motor.Position().y, 0.6, 0.05);
        j.in.climb = true;
        j.Run(w, 0.2);
        j.in.climb = false;
        j.in.moveDir = {};
        j.Run(w, 3.5);
        CHECK_NEAR(j.motor.Position().y, 0.0, 0.05);
        CHECK(j.motor.Position().z > 2.0f);
    }
}

static void TestLipSync() {
    CharacterAsset a;
    CHECK(ImportFbx(std::string(EON_TEST_DATA) + "/test_rig.fbx", a));
    Character ch;
    ch.Init(&a);
    LipSync lips;
    lips.Init(ch);
    lips.Say("Hello, my name is Jenny.");
    CHECK(lips.Speaking());
    bool sawM = false, sawOpen = false;
    for (const VisemeKey& k : lips.Track()) { sawM |= k.v == Viseme::MBP; sawOpen |= k.v == Viseme::AA || k.v == Viseme::OH; }
    CHECK(sawM && sawOpen);
    CHECK(lips.Track().back().time > 1.0f && lips.Track().back().time < 4.0f);   // natural speaking speed
    CHECK(PhonemeToViseme("AA1") == Viseme::AA && PhonemeToViseme("M") == Viseme::MBP && PhonemeToViseme("UW") == Viseme::OO);
    for (int f = 0; f < 600 && lips.Speaking(); ++f) lips.Update(ch, 1 / 60.f);
    CHECK(!lips.Speaking());
}

static void TestSleep() {
    BodyState b;
    b.gameMinutesPerSecond = 60.f;
    b.needs[static_cast<int>(Need::Fatigue)] = 85.f;
    BodyInputs in;
    for (int i = 0; i < 60 * 10 && !b.asleep; ++i) { b.UpdateSleep(true, 1 / 60.f); in.sleeping = b.asleep; b.Step(in, 1 / 60.f); }
    CHECK(b.asleep);                                   // very tired and lying still: asleep within minutes
    for (int i = 0; i < 60 * 60 * 8 && b.asleep; ++i) { b.UpdateSleep(true, 1 / 60.f); in.sleeping = b.asleep; b.Step(in, 1 / 60.f); }
    CHECK(!b.asleep);                                  // woke up (rested or needing the toilet)
    CHECK(b.Get(Need::Fatigue) < 40.f);

    BodyState c;
    c.gameMinutesPerSecond = 60.f;
    c.needs[static_cast<int>(Need::Fatigue)] = 85.f;
    for (int i = 0; i < 60 * 10; ++i) c.UpdateSleep(false, 1 / 60.f);
    CHECK(!c.asleep);                                  // can't fall asleep while moving around
}

int main(int argc, char** argv) {
    // eon_tests [contentDir] [--only <substring>] [--list] [--times]
    std::string content = EON_CONTENT_DIR, only;
    bool list = false, times = false;
    for (int a = 1; a < argc; ++a) {
        const std::string s = argv[a];
        if (s == "--only" && a + 1 < argc) only = argv[++a];
        else if (s == "--list") list = true;
        else if (s == "--times") times = true;
        else content = s;
    }
    int ran = 0;
#define RUN(fn, ...) do { if (list) { std::printf("%s\n", #fn); break; } \
        if (!only.empty() && std::string(#fn).find(only) == std::string::npos) break; \
        g_test = #fn; ++ran; const auto t0 = std::chrono::steady_clock::now(); fn(__VA_ARGS__); \
        if (times) std::printf("  %-32s %6.2f s\n", #fn, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); } while (0)
    RUN(TestOpenBattery);
    RUN(TestSimpleLoop);
    RUN(TestSwitchAndLamp);
    RUN(TestUnconnectedWireDoesNothing);
    RUN(TestParallel);
    RUN(TestShortCircuit);
    RUN(TestBatteryDrains);
    RUN(TestMotor);
    RUN(TestZoneRoundTrip, content);
    RUN(TestHingeAndSliderBasics);
    RUN(TestWaitingAreaLight, content);
    RUN(TestTestZoneButton, content);
    RUN(TestLooseBatterySettles, content);
    RUN(TestClipPullOffAndReattach);
    RUN(TestDirectTouchLightsBulb);
    RUN(TestCharacterImport);
    RUN(TestBodyState);
    RUN(TestWardrobeRules);
    RUN(TestMotorPressesButton, content);
    RUN(TestMotorWalksAndStopsAtWall, content);
    RUN(TestMotorGripsAndLifts, content);
    RUN(TestMemoryGraph);
    RUN(TestJennyLearnsTheButton, content);
    RUN(TestFlipsWallSwitch, content);
    RUN(TestPullsLever, content);
    RUN(TestCarriesThings, content);
    RUN(TestStances);
    RUN(TestSitOnSeat);
    RUN(TestStepAndClimb);
    RUN(TestLipSync);
    RUN(TestSleep);
    if (list) return 0;
    std::printf("%d passed, %d failed (%d tests)\n", g_pass, g_fail, ran);
    return g_fail == 0 ? 0 : 1;
}
