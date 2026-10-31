// One physics scenario run through our own Jolt Physics 5.5.0 builds, to show that the
// Windows (MSVC ABI) and the Linux (Itanium ABI) builds compute identical bits.
// No X4 code or data: our scenario, Jolt's public API only. It exercises what X4's XPhys
// layer uses (box, sphere, capsule, cylinder, convex hull, static and mutable compound,
// rotated/translated, offset-centre-of-mass and mesh shapes, BodyInterface velocity,
// impulse and torque calls, constraints, CharacterVirtual::ExtendedUpdate, ray and shape
// queries, a job system with several threads) and hashes every body each frame plus
// the complete PhysicsSystem::SaveState every 60 frames.
//   jolt_determinism_scenario [frames=600] [threads=4]
// Output: "frame N <body hash> <query hash>" per frame, "state N <hash>" every 60
// frames, then "final <hash>". Equal output on two builds = bit-identical simulation.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace JPH;

namespace {
namespace Layers { constexpr ObjectLayer STATIC = 0, MOVING = 1, COUNT = 2; }
namespace BPLayers { constexpr BroadPhaseLayer STATIC(0), MOVING(1); constexpr uint COUNT = 2; }

class BPLayerInterface final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BPLayers::COUNT; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override { return inLayer == Layers::STATIC ? BPLayers::STATIC : BPLayers::MOVING; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "layer"; }
#endif
};
class ObjectVsBP final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer inLayer, BroadPhaseLayer inBP) const override { return inLayer == Layers::MOVING || inBP == BPLayers::MOVING; }
};
class ObjectPair final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == Layers::MOVING || b == Layers::MOVING; }
};
// Contact callback with a deterministic material rule, like a game's listener.
class Contacts final : public ContactListener {
public:
    void OnContactAdded(const Body &a, const Body &b, const ContactManifold &m, ContactSettings &s) override {
        s.mCombinedFriction = 0.5f * (a.GetFriction() + b.GetFriction());
        s.mCombinedRestitution = a.GetRestitution() > b.GetRestitution() ? a.GetRestitution() : b.GetRestitution();
        (void)m;
    }
    void OnContactPersisted(const Body &a, const Body &b, const ContactManifold &m, ContactSettings &s) override { OnContactAdded(a, b, m, s); }
};

// Fixed linear congruential generator: the scenario never depends on the platform's rand().
struct Lcg {
    uint64 s;
    uint32 next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return uint32(s >> 33); }
    float unit() { return float(next() & 0xffffff) / float(0x1000000); } // [0,1), exact
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
};

struct Fnv {
    uint64 h = 1469598103934665603ull;
    void bytes(const void *p, size_t n) { const uint8 *b = static_cast<const uint8 *>(p); for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } }
    void f(float v) { uint32 u; std::memcpy(&u, &v, 4); bytes(&u, 4); }
    void v3(Vec3Arg v) { f(v.GetX()); f(v.GetY()); f(v.GetZ()); }
    void q(QuatArg v) { f(v.GetX()); f(v.GetY()); f(v.GetZ()); f(v.GetW()); }
};

RefConst<Shape> Make(const ShapeSettings &settings) {
    ShapeSettings::ShapeResult r = settings.Create();
    if (r.HasError()) { std::fprintf(stderr, "shape error: %s\n", r.GetError().c_str()); std::exit(2); }
    return r.Get();
}
} // namespace

#ifdef JOLT_SCENARIO_X4
void X4JoltLoad(); // x4_linux_jolt_loader.cpp: drive the owned executable's Jolt instead of ours
#endif

int main(int argc, char **argv) {
    int frames = argc > 1 ? std::atoi(argv[1]) : 600;
    int threads = argc > 2 ? std::atoi(argv[2]) : 4;
    int dumpFrame = argc > 3 ? std::atoi(argv[3]) : 0; // optional: per-body rows at this frame
#ifdef JOLT_SCENARIO_X4
    X4JoltLoad();
#endif
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();
    {
        TempAllocatorImpl temp(16 * 1024 * 1024);
        JobSystemThreadPool jobs(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);
        BPLayerInterface bp; ObjectVsBP ovbp; ObjectPair pair; Contacts contacts;
        PhysicsSystem physics;
        physics.Init(4096, 0, 8192, 8192, bp, ovbp, pair);
        physics.SetContactListener(&contacts);
        BodyInterface &bi = physics.GetBodyInterface();
        Lcg rng{0x58344d50ull};

        // Static ground: a box and a mesh terrain patch.
        bi.CreateAndAddBody(BodyCreationSettings(new BoxShapeSettings(Vec3(200.0f, 1.0f, 200.0f)), RVec3(0, -1, 0), Quat::sIdentity(), EMotionType::Static, Layers::STATIC), EActivation::DontActivate);
        TriangleList tris;
        for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) {
            float x0 = 40.0f + 3.0f * x, z0 = -24.0f + 3.0f * z;
            float h00 = 0.3f * rng.unit(), h10 = 0.3f * rng.unit(), h01 = 0.3f * rng.unit(), h11 = 0.3f * rng.unit();
            tris.push_back(Triangle(Float3(x0, h00, z0), Float3(x0, h01, z0 + 3), Float3(x0 + 3, h10, z0)));
            tris.push_back(Triangle(Float3(x0 + 3, h10, z0), Float3(x0, h01, z0 + 3), Float3(x0 + 3, h11, z0 + 3)));
        }
        bi.CreateAndAddBody(BodyCreationSettings(new MeshShapeSettings(tris), RVec3::sZero(), Quat::sIdentity(), EMotionType::Static, Layers::STATIC), EActivation::DontActivate);

        // Dynamic bodies of every shape kind in a deterministic jumble.
        std::vector<BodyID> dynamic;
        RefConst<Shape> hull;
        {
            Array<Vec3> points;
            for (int i = 0; i < 24; i++) points.push_back(Vec3(rng.range(-0.8f, 0.8f), rng.range(-0.6f, 0.6f), rng.range(-1.0f, 1.0f)));
            hull = Make(ConvexHullShapeSettings(points));
        }
        StaticCompoundShapeSettings compoundSettings;
        compoundSettings.AddShape(Vec3(0, 0, 0), Quat::sIdentity(), new BoxShapeSettings(Vec3(1.0f, 0.2f, 0.5f)));
        compoundSettings.AddShape(Vec3(0.8f, 0.3f, 0), Quat::sRotation(Vec3::sAxisZ(), 0.4f), new SphereShapeSettings(0.35f));
        RefConst<Shape> compound = Make(compoundSettings);
        MutableCompoundShapeSettings mutableSettings;
        mutableSettings.AddShape(Vec3(0, 0, 0), Quat::sIdentity(), new CapsuleShapeSettings(0.5f, 0.25f));
        mutableSettings.AddShape(Vec3(0, 0.7f, 0), Quat::sIdentity(), new BoxShapeSettings(Vec3(0.3f, 0.3f, 0.3f)));
        RefConst<Shape> mutableCompound = Make(mutableSettings);
        RefConst<Shape> shapes[] = {
            Make(BoxShapeSettings(Vec3(0.5f, 0.5f, 0.5f))), Make(SphereShapeSettings(0.6f)), Make(CapsuleShapeSettings(0.6f, 0.3f)),
            Make(CylinderShapeSettings(0.5f, 0.4f)), hull, compound,
            Make(RotatedTranslatedShapeSettings(Vec3(0.2f, 0, 0), Quat::sRotation(Vec3::sAxisX(), 0.7f), new BoxShapeSettings(Vec3(0.6f, 0.2f, 0.3f)))),
            Make(OffsetCenterOfMassShapeSettings(Vec3(0.1f, -0.2f, 0), new SphereShapeSettings(0.45f))),
        };
        constexpr int kinds = sizeof(shapes) / sizeof(shapes[0]);
        for (int i = 0; i < 160; i++) {
            RVec3 at(rng.range(-12.0f, 12.0f) + (i % 4 == 3 ? 50.0f : 0.0f), 2.0f + 0.9f * float(i / 8), rng.range(-12.0f, 12.0f));
            Quat rot = Quat::sRotation(Vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)).NormalizedOr(Vec3::sAxisY()), rng.range(0.0f, 3.0f));
            BodyCreationSettings s(shapes[i % kinds], at, rot, EMotionType::Dynamic, Layers::MOVING);
            s.mFriction = rng.range(0.2f, 0.9f);
            s.mRestitution = rng.range(0.0f, 0.4f);
            s.mLinearDamping = 0.05f;
            if (i % 11 == 0) s.mMotionQuality = EMotionQuality::LinearCast;
            dynamic.push_back(bi.CreateAndAddBody(s, EActivation::Activate));
        }
        BodyID mutableBody = bi.CreateAndAddBody(BodyCreationSettings(mutableCompound, RVec3(-20, 3, -20), Quat::sIdentity(), EMotionType::Dynamic, Layers::MOVING), EActivation::Activate);
        BodyID kinematic = bi.CreateAndAddBody(BodyCreationSettings(Make(BoxShapeSettings(Vec3(3.0f, 0.3f, 3.0f))), RVec3(0, 0.5f, 25), Quat::sIdentity(), EMotionType::Kinematic, Layers::MOVING), EActivation::Activate);

        // A small hinge and distance chain.
        {
            BodyLockMultiWrite lock(physics.GetBodyLockInterface(), dynamic.data(), 4);
            Body *b0 = lock.GetBody(0), *b1 = lock.GetBody(1), *b2 = lock.GetBody(2), *b3 = lock.GetBody(3);
            HingeConstraintSettings hinge;
            hinge.mPoint1 = hinge.mPoint2 = b0->GetCenterOfMassPosition();
            hinge.mHingeAxis1 = hinge.mHingeAxis2 = Vec3::sAxisY();
            hinge.mNormalAxis1 = hinge.mNormalAxis2 = Vec3::sAxisX();
            physics.AddConstraint(hinge.Create(*b0, *b1));
            DistanceConstraintSettings dist;
            dist.mPoint1 = b1->GetCenterOfMassPosition(); dist.mPoint2 = b2->GetCenterOfMassPosition();
            physics.AddConstraint(dist.Create(*b1, *b2));
            PointConstraintSettings point;
            point.mPoint1 = point.mPoint2 = b3->GetCenterOfMassPosition();
            physics.AddConstraint(point.Create(*b2, *b3));
        }

        // A character walking a fixed route.
        CharacterVirtualSettings cs;
        cs.mShape = Make(CapsuleShapeSettings(0.9f, 0.3f));
        cs.mSupportingVolume = Plane(Vec3::sAxisY(), -0.3f);
        Ref<CharacterVirtual> character = new CharacterVirtual(&cs, RVec3(-30, 1.5f, 0), Quat::sIdentity(), 0, &physics);
        CharacterVirtual::ExtendedUpdateSettings eus;

        physics.OptimizeBroadPhase();
        Fnv final;
        const float dt = 1.0f / 60.0f;
        for (int frame = 1; frame <= frames; frame++) {
            // Game-like inputs: impulses, torques, velocities, a kinematic platform, a shape edit.
            BodyID push = dynamic[size_t(frame * 7) % dynamic.size()];
            bi.AddImpulse(push, Vec3(0.0f, 9.0f, 1.5f), bi.GetCenterOfMassPosition(push) + Vec3(0.1f, 0, 0));
            bi.AddTorque(dynamic[size_t(frame * 13) % dynamic.size()], Vec3(0.0f, 40.0f, 0.0f));
            bi.AddAngularImpulse(dynamic[size_t(frame * 5) % dynamic.size()], Vec3(0.3f, 0, 0.2f));
            if (frame % 90 == 0) bi.SetLinearAndAngularVelocity(dynamic[size_t(frame) % dynamic.size()], Vec3(2.0f, 6.0f, -1.0f), Vec3(0, 1.0f, 0));
            bi.MoveKinematic(kinematic, RVec3(8.0f * Sin(0.02f * float(frame)), 0.5f, 25.0f), Quat::sRotation(Vec3::sAxisY(), 0.01f * float(frame)), dt);
            if (frame % 120 == 60) {
                // Edit the compound under the body lock, then notify without it (NotifyShapeChanged locks the body).
                Vec3 com = Vec3::sZero();
                bool edited = false;
                {
                    BodyLockWrite lock(physics.GetBodyLockInterface(), mutableBody);
                    if (lock.Succeeded()) {
                        MutableCompoundShape *m = static_cast<MutableCompoundShape *>(const_cast<Shape *>(lock.GetBody().GetShape()));
                        com = m->GetCenterOfMass();
                        m->ModifyShape(1, Vec3(0, 0.7f + 0.1f * float(frame / 120), 0), Quat::sRotation(Vec3::sAxisZ(), 0.1f * float(frame / 120)));
                        m->AdjustCenterOfMass();
                        edited = true;
                    }
                }
                if (edited) bi.NotifyShapeChanged(mutableBody, com, true, EActivation::Activate);
            }
            character->SetLinearVelocity(Vec3(3.0f * Cos(0.01f * float(frame)), 0.0f, 3.0f * Sin(0.01f * float(frame))) + character->GetLinearVelocity() * Vec3(0, 1, 0) + physics.GetGravity() * dt);
            character->ExtendedUpdate(dt, physics.GetGravity(), eus, physics.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                                      physics.GetDefaultLayerFilter(Layers::MOVING), {}, {}, temp);
            EPhysicsUpdateError error = physics.Update(dt, 1, &temp, &jobs);
            if (error != EPhysicsUpdateError::None) { std::printf("update_error %d %u\n", frame, unsigned(error)); return 3; }

            Fnv bodies;
            BodyIDVector ids; physics.GetBodies(ids);
            for (const BodyID &id : ids) {
                bodies.bytes(&id, sizeof(id));
                bodies.v3(Vec3(bi.GetPosition(id))); bodies.q(bi.GetRotation(id));
                bodies.v3(bi.GetLinearVelocity(id)); bodies.v3(bi.GetAngularVelocity(id));
                if (frame == dumpFrame) {
                    // Diagnosis: one row per body, so two runs can be diffed body by body.
                    Fnv one; one.v3(Vec3(bi.GetPosition(id))); one.q(bi.GetRotation(id)); one.v3(bi.GetLinearVelocity(id)); one.v3(bi.GetAngularVelocity(id));
                    std::printf("body %u shape %u/%u %016llx\n", id.GetIndex(), unsigned(bi.GetShape(id)->GetType()),
                                unsigned(bi.GetShape(id)->GetSubType()), (unsigned long long)one.h);
                }
            }
            bodies.v3(Vec3(character->GetPosition())); bodies.v3(character->GetLinearVelocity());
            uint32 ground = uint32(character->GetGroundState()); bodies.bytes(&ground, 4);
            // Queries the game makes: a ray down a fixed lattice and a sphere sweep.
            Fnv queries;
            for (int k = 0; k < 16; k++) {
                RRayCast ray(RVec3(-16.0f + 2.0f * k, 30.0f, 3.0f * Sin(float(k + frame))), Vec3(0, -40.0f, 0));
                RayCastResult hit;
                if (physics.GetNarrowPhaseQuery().CastRay(ray, hit)) { queries.bytes(&hit.mBodyID, sizeof(hit.mBodyID)); queries.f(hit.mFraction); }
            }
            ClosestHitCollisionCollector<CastShapeCollector> sweep;
            RefConst<Shape> probe = shapes[1];
            RShapeCast cast(probe, Vec3::sOne(), RMat44::sTranslation(RVec3(-10, 20, 0)), Vec3(20, -25, 5));
            physics.GetNarrowPhaseQuery().CastShape(cast, ShapeCastSettings(), RVec3::sZero(), sweep);
            if (sweep.HadHit()) { queries.f(sweep.mHit.mFraction); queries.v3(sweep.mHit.mPenetrationAxis); }
            std::printf("frame %d %016llx %016llx\n", frame, (unsigned long long)bodies.h, (unsigned long long)queries.h);
            std::fflush(stdout);
            final.bytes(&bodies.h, 8); final.bytes(&queries.h, 8);
            if (frame % 60 == 0) {
                StateRecorderImpl state;
                physics.SaveState(state);
                std::string data = state.GetData();
                Fnv s; s.bytes(data.data(), data.size());
                std::printf("state %d %016llx %zu\n", frame, (unsigned long long)s.h, data.size());
                final.bytes(&s.h, 8);
            }
        }
        std::printf("final %016llx\n", (unsigned long long)final.h);
        character = nullptr;
    }
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
    std::fflush(stdout);
#ifdef JOLT_SCENARIO_X4
    std::_Exit(0); // the executable's own exit handlers belong to the game process, not this test
#endif
    return 0;
}
