#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "Circuit.h"
#include "Math.h"
#include "Physics.h"
#include "Zone.h"

struct lua_State;

namespace eon {

// What the renderer draws. Contains no names/types: safe to feed to Jenny's eye cameras.
struct RenderPiece {
    Vec3 pos;
    Quat rot;
    Vec3 size;          // full box extents (capsules are drawn as their bounding box)
    Rgb color;
    float emissive = 0.f;
    int entity = 0;     // operator/editor highlighting only; stripped before the mind bridge
};

struct RenderLight {
    Vec3 pos;
    Vec3 color;         // linear intensity
};

struct RenderFrame {
    std::vector<RenderPiece> pieces;
    std::vector<RenderLight> lights;
};

struct RayHit {
    int body = -1;      // runtime body index
    int entity = 0;
    Vec3 point;
    float dist = 0.f;
};

// ---- Matter: liquids, food, heat (World_Matter.cpp) ----
enum class Liquid : uint8_t { Water, Urine, Count };
const char* LiquidName(Liquid l);

// What reached a body opening this step (her mouth), and what landed on her skin.
struct Intake {
    float ml[static_cast<int>(Liquid::Count)] = {};   // swallowed
    float kcal = 0.f;                                 // food bitten off
    float foodG = 0.f;
    float skinMl = 0.f;                               // liquid that landed on her body
    float skinUrineMl = 0.f;
};

// Runs one zone: Jolt physics + electricity, coupled every fixed step.
class World {
public:
    static constexpr double kFixedDt = 1.0 / 120.0;

    World();
    ~World();

    bool Load(const std::string& path, std::string* error = nullptr);
    void SetZone(const Zone& zone);   // load from memory
    void Reset();                     // rebuild runtime from the zone layout
    void Tick(double realDt);         // real-time frame step (clamped against hitches)
    void Advance(double seconds);     // exact simulated time (tests/tools)
    void SetPhysicsEnabled(bool on) { physicsOn_ = on; }
    bool PhysicsEnabled() const { return physicsOn_; }

    // ---- Physical interaction (operator now, Jenny's hands later) ----
    bool RayCast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out, bool skipAgent = false) const;
    Vec3 SurfaceNormal(int body, const Vec3& point) const;
    float BodyMass(int body) const;   // 0 = static/immovable

    // ---- Agent bodies (Jenny's torso/hands live in the same physics as everything else) ----
    int AddAgentBody(const BodyDesc& d);
    // Each fixed step a force (capped at maxForce N) drives the body toward target, like a muscle.
    void SetDrive(int body, const Vec3& target, float maxForce, float frequency = 12.f, float damping = 1.f);
    void ClearDrive(int body);
    void SetKinematicTarget(int body, const Vec3& pos, const Quat& rot);
    // Rigid joint between two bodies at a point (a grip). Returns a handle for Detach/JointForce.
    int Attach(int bodyA, int bodyB, const Vec3& worldPoint);
    void Detach(int joint);
    float JointForce(int joint) const;   // N during the last step
    Vec3 BodyVelocity(int body) const;
    Quat BodyRotation(int body) const;
    void BeginGrab(int body, const Vec3& worldPoint);
    void SetGrabTarget(const Vec3& worldTarget);
    void EndGrab();
    int GrabbedBody() const { return grab_.body; }
    Vec3 GrabPoint() const;
    // Continuous push force at a point on a body, until cleared (or for `seconds` if > 0).
    void SetPush(int body, const Vec3& worldPoint, const Vec3& force, float seconds = -1.f);
    void ClearPush();
    // Wrists holding something steady: a capped torque turns the body toward `rot` (like gripping hands resist twisting).
    void SetSteady(int body, const Quat& rot, float maxTorque);
    void ClearSteady(int body);

    // Debug actuation (editor/tests): physically presses a button or flips a switch.
    void DebugActuate(int entityId, bool on);

    // ---- State ----
    Zone& GetZone() { return zone_; }
    const Zone& GetZone() const { return zone_; }
    Circuit& GetCircuit() { return circuit_; }
    const Circuit& GetCircuit() const { return circuit_; }
    const Part* PartOf(int entityId) const;
    double SimTime() const { return simTime_; }
    int Generation() const { return generation_; }   // bumps on every Reset (agent bodies must be re-added)
    const std::string& Path() const { return path_; }

    int EntityOfBody(int body) const;
    BodyHandle HandleOf(int body) const;
    Vec3 BodyPosition(int body) const;
    int MainBodyOf(int entityId) const;
    Physics& GetPhysics() { return *physics_; }

    // Electrical contact state, for tests and the editor overlay.
    int ClipAttachedEntity(int wireEntityId, int end) const;   // entity the clip is latched to, or 0
    float SwitchAngle(int entityId) const;                     // rocker/lever angle (rad), tests/editor only
    int ClipBodyOf(int wireEntityId, int end) const;
    std::vector<Vec3> TerminalPositions(int entityId) const;

    void BuildRenderFrame(RenderFrame& out) const;

    // ---- Matter (liquids flow, fill, pour, puddle and evaporate; food gets bitten; heaters warm the air) ----
    float AirTemperature(const Vec3& p) const;           // °C here (zone ambient + nearby heaters)
    // Her mouth this step: open mouths take in liquid that reaches them. Bite() takes a bite of food at the mouth.
    void SetMouth(const Vec3& pos, bool open) { mouth_ = pos; mouthOpen_ = open; hasMouth_ = true; }
    float Bite(const Vec3& mouth, float reach = 0.07f);   // grams bitten (0 = nothing there)
    // A liquid stream leaving her body this step (peeing): origin, initial velocity, ml/s.
    void SetBodyStream(const Vec3& origin, const Vec3& vel, float mlPerSec, Liquid l);
    void ClearBodyStream() { bodyStream_.rate = 0.f; }
    Intake TakeIntake();                                  // accumulated since the last call
    // Tests/editor: liquid held by a container or bowl entity, and total in puddles.
    float ContainerMl(int entityId, Liquid l = Liquid::Count) const;   // Count = all liquids
    float PuddleMl(Liquid l = Liquid::Count) const;
    int PuddleCount() const;
    float FoodLeftG(int entityId) const;

    // ---- Object scripts (World_Scripts.cpp): Lua in an entity's "script" (file under the content root) or "script_src" ----
    // A script is the object's own machinery (a vending machine's logic, a thermostat): it may read and drive the
    // object's parts. It is world-side: nothing it knows reaches Jenny except through what she can perceive.
    void SetContentRoot(const std::string& dir) { contentRoot_ = dir; }
    const std::string& ContentRoot() const { return contentRoot_; }
    int Spawn(Entity e);                                  // add a new object at runtime (returns its id)
    // Places a whole object from content/objects at runtime (scripts, operator tools). Returns the instance's
    // name ("" on failure); its parts are "<name>.<Part>". Removed again by Reset, like everything spawned.
    // Doors (type "door"): a dynamic panel on a hinge along one vertical edge. params: hingeSide left|right,
    // frame (name of what it hangs on; none = the world), swing (deg, 100), seal (N*m holding it shut, magnet/latch),
    // friction (N*m), locked. Opening swings its free edge toward its local +z (the side it faces).
    float DoorAngle(int entityId) const;                  // degrees open (0 = shut)
    bool DoorShut(int entityId) const { return DoorAngle(entityId) < 1.5f; }
    void SetDoorLocked(int entityId, bool locked);        // a bolt: holds it once it's shut
    bool DoorLocked(int entityId) const;
    std::string SpawnObject(const std::string& object, const Vec3& pos, float yawDeg = 0.f, const nlohmann::json& set = nlohmann::json::object());
    void ReloadScripts();                                 // re-read script files (hot reload while running)
    const std::vector<std::string>& ScriptLog() const { return scriptLog_; }
    void ScriptLogLine(const std::string& s);
    int ScriptCount() const { return static_cast<int>(scripts_.size()); }
    bool AgentNear(const Vec3& p, float r) const;       // something of hers (torso/hand) within r (a motion sensor)
    void SetRelay(int entityId, bool closed);            // a relay is a switch only scripts throw
    void SetTapScriptOpen(int entityId, bool open);
    void SetEmissive(int entityId, float e);
    void SetColor(int entityId, Rgb c);

private:
    enum class Role : uint8_t { Main, Plunger, Lever, RopeSegment, Clip, Agent };

    struct RtBody {
        BodyHandle h = kNoBody;
        int entity = 0;
        Role role = Role::Main;
        Vec3 size;           // full extents for drawing
        Quat drawOffset;     // capsule: box drawn along its Y
        Rgb color;
    };
    struct Terminal {
        int body = -1;       // -1: not physical (ideal)
        Vec3 local;
        int part = -1;
        int term = 0;
        int entity = 0;
        int clip = -1;       // index into clips_ if this terminal is a wire clip
    };
    struct Clip {
        int body = -1;
        int terminal = -1;   // its own terminal
        int attachedTo = -1; // terminal index it is latched onto
        ConstraintHandle joint = kNoConstraint;
        float cooldown = 0.f;
        float breakForce = 12.f;
    };
    struct ButtonRt {
        int entity = 0, base = -1, plunger = -1;
        ConstraintHandle slider = kNoConstraint;
        float travel = 0.008f, force = 2.5f;
        Vec3 axisLocal{0, 1, 0};
    };
    struct SwitchRt {
        int entity = 0, base = -1, lever = -1;
        ConstraintHandle hinge = kNoConstraint;
        Vec3 axisLocal{0, 1, 0}, hingeLocal{0, 0, 1}, longLocal{1, 0, 0};
        float limit = 0.3f, detent = 0.03f, halfLong = 0.02f, halfP = 0.006f;
        bool isLever = false;
        float handleHalf = 0.f;
        Vec3 pivot;
    };
    struct IdealLink { int partA, termA, partB, termB; };
    struct PushRt { int body = -1; Vec3 local; Vec3 force; float timeLeft = -1.f; int tag = 0; };
    // A grab is a kinematic, non-colliding "hand" point pinned to the object: strong like a real grip,
    // speed-limited like a real arm, and it slips if the object resists harder than maxForce.
    struct GrabRt {
        int body = -1;
        Vec3 local, target, handPos;
        float maxForce = 60.f, maxSpeed = 4.f;
        BodyHandle hand = kNoBody;
        ConstraintHandle joint = kNoConstraint;
        int overloadSteps = 0;   // consecutive steps above maxForce
    };

    void BuildEntity(Entity& e);
    void BuildRope(Entity& wire, const std::vector<std::pair<int, int>>& endLinks);
    int AddBody(const BodyDesc& d, int entity, Role role, const Vec3& drawSize, Rgb color, Quat drawOffset = {});
    int AddTerminal(int body, const Vec3& local, int part, int term, int entity);
    int FindTerminal(int entity, int term) const;
    Vec3 TerminalWorld(const Terminal& t) const;
    Vec3 ToLocal(int body, const Vec3& world) const;
    Vec3 ToWorld(int body, const Vec3& local) const;
    void AttachClip(int clipIndex, int terminalIndex);
    void DetachClip(int clipIndex);

    void FixedStep(float dt);
    void ApplyForces(float dt);
    void UpdateClips(float dt);
    void UpdateSwitchStates();
    void UpdateConnections();

    Zone zone_;
    Circuit circuit_;
    std::unique_ptr<Physics> physics_;
    std::string path_;

    std::vector<RtBody> bodies_;
    std::vector<Terminal> terminals_;
    std::vector<Clip> clips_;
    std::vector<ButtonRt> buttons_;
    std::vector<SwitchRt> switches_;
    std::vector<IdealLink> idealLinks_;
    std::vector<PushRt> pushes_;
    GrabRt grab_;
    struct DriveRt { int body = -1; Vec3 target; float maxForce = 0.f, freq = 12.f, damping = 1.f; };
    // matter runtime
    struct TapRt { int entity = 0, body = -1; Vec3 outLocal, dirLocal{0, -1, 0}; float rate = 60.f, speed = 0.6f; int valve = 0; bool powered = false; Liquid kind = Liquid::Water; int jets = 1; float spread = 0.f; bool scripted = false, scriptOpen = false; };
    struct ContainerRt { int entity = 0, body = -1; float capacity = 300.f; float ml[int(Liquid::Count)] = {}; Vec3 half; int drain = 0; bool dirtyDrain = false; };
    struct PuddleRt { Vec3 pos; float ml[int(Liquid::Count)] = {}; int id = 0; };
    struct FoodRt { int entity = 0, body = -1; float grams = 100.f, left = 100.f, kcalPerG = 0.6f, waterPerG = 0.8f; Vec3 size; bool gone = false; };
    struct HeaterRt { int entity = 0, body = -1; };
    struct DoorRt { int entity = 0, body = -1; ConstraintHandle hinge = kNoConstraint; bool built = false, locked = false, held = false; float swing = 1.75f, seal = 0.f, friction = 0.3f; Vec3 axisW; };
    // a closed space with its own air (fridge, freezer, oven): temperature inside its box drifts toward `airC`
    // while its door is shut and toward the room while it's open
    struct ColdRt { int entity = 0, door = 0; Vec3 lo, hi; float target = 4.f, cur = 4.f, tauShut = 600.f, tauOpen = 90.f; };
    void BuildDoor(const Entity& e, int mainBody);
    void StepDoors(float dt);
    std::vector<DoorRt> doors_;
    std::vector<ColdRt> colds_;
    struct StreamRt { Vec3 origin, vel; float rate = 0.f; Liquid kind = Liquid::Water; int fromContainer = -1; bool fromBody = false; };
    struct StreamDraw { std::vector<Vec3> pts; Liquid kind; };
    void BuildMatter(Entity& e, int mainBody);
    void StepMatter(float dt);
    void FlowStream(const StreamRt& s, float dt, StreamDraw* draw);
    void AddToPuddle(const Vec3& p, Liquid l, float ml);
    void AppendMatterPieces(RenderFrame& out) const;
    bool ValveOpen(int valveEntity) const;
    std::vector<TapRt> taps_;
    std::vector<ContainerRt> containers_;
    std::vector<PuddleRt> puddles_;
    std::vector<FoodRt> foods_;
    std::vector<HeaterRt> heaters_;
    std::vector<StreamDraw> streamDraw_;
    StreamRt bodyStream_;
    Intake intake_;
    Vec3 mouth_;
    bool mouthOpen_ = false, hasMouth_ = false;
    int nextPuddleId_ = 1;
    // scripts runtime
    struct ScriptRt { int entity = 0; int env = -1; bool broken = false, lastClosed = false; std::string source, file; };
    void StartScripts();
    void StepScripts(float dt);
    void StopScripts();
    bool LoadScript(ScriptRt& s);
    lua_State* L_ = nullptr;
    std::vector<ScriptRt> scripts_;
    std::vector<std::string> scriptLog_;
    std::string contentRoot_;
    float scriptAcc_ = 0.f;
    std::unordered_map<int, float> emissiveOverride_;
    std::unordered_map<int, Rgb> colorOverride_;
    struct KinRt { int body = -1; Vec3 pos; Quat rot; };
    std::vector<DriveRt> drives_;
    struct SteadyRt { int body = -1; Quat rot; float maxTorque = 0.f; };
    std::vector<SteadyRt> steady_;
    std::vector<KinRt> kinematic_;
    std::vector<ConstraintHandle> joints_;
    float lastDt_ = static_cast<float>(kFixedDt);
    int generation_ = 0;

    bool physicsOn_ = true;
    double accumulator_ = 0.0;
    double simTime_ = 0.0;
};

} // namespace eon
