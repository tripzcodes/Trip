// Jolt requires these before including headers
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <engine/physics/physics_world.h>
#include <engine/scene/scene.h>
#include <engine/scene/components.h>

JPH_SUPPRESS_WARNINGS

using namespace JPH;

// Jolt requires layer definitions
namespace Layers {
    static constexpr ObjectLayer NON_MOVING = 0;
    static constexpr ObjectLayer MOVING = 1;
    static constexpr uint32_t NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr BroadPhaseLayer NON_MOVING(0);
    static constexpr BroadPhaseLayer MOVING(1);
    static constexpr uint32_t NUM_LAYERS = 2;
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return layer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        return layer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
    }
#endif
};

class OVBPLayerFilter final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer obj, BroadPhaseLayer bp) const override {
        if (obj == Layers::NON_MOVING) { return bp == BroadPhaseLayers::MOVING; }
        return true;
    }
};

class OVOLayerFilter final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        if (a == Layers::NON_MOVING && b == Layers::NON_MOVING) { return false; }
        return true;
    }
};

namespace engine {

struct PhysicsWorld::Impl {
    std::unique_ptr<TempAllocatorImpl> temp_allocator;
    std::unique_ptr<JobSystemThreadPool> job_system;
    std::unique_ptr<PhysicsSystem> physics_system;

    BPLayerInterfaceImpl bp_layer_interface;
    OVBPLayerFilter ovbp_filter;
    OVOLayerFilter ovo_filter;

    std::unordered_map<entt::entity, BodyID> entity_to_body;
    std::unordered_map<uint32_t, entt::entity> body_to_entity;
};

PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();

    impl_->temp_allocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);
    impl_->job_system = std::make_unique<JobSystemThreadPool>(
        cMaxPhysicsJobs, cMaxPhysicsBarriers, 2);

    const uint32_t max_bodies = 10240;
    const uint32_t num_mutexes = 0;
    const uint32_t max_body_pairs = 10240;
    const uint32_t max_contacts = 10240;

    impl_->physics_system = std::make_unique<PhysicsSystem>();
    impl_->physics_system->Init(max_bodies, num_mutexes, max_body_pairs, max_contacts,
        impl_->bp_layer_interface, impl_->ovbp_filter, impl_->ovo_filter);

    impl_->physics_system->SetGravity(Vec3(0.0f, -9.81f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() {
    UnregisterTypes();
    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

void PhysicsWorld::sync_from_scene(Scene& scene) {
    auto& body_interface = impl_->physics_system->GetBodyInterface();

    // remove Jolt bodies for destroyed entities (chunk unloading, etc.)
    for (auto it = impl_->entity_to_body.begin(); it != impl_->entity_to_body.end(); ) {
        if (!scene.registry().valid(it->first) ||
            !scene.registry().all_of<RigidBodyComponent>(it->first)) {
            body_interface.RemoveBody(it->second);
            body_interface.DestroyBody(it->second);
            impl_->body_to_entity.erase(it->second.GetIndexAndSequenceNumber());
            it = impl_->entity_to_body.erase(it);
        } else {
            ++it;
        }
    }

    auto view = scene.registry().view<TransformComponent, RigidBodyComponent>();
    for (auto entity : view) {
        auto& rb = view.get<RigidBodyComponent>(entity);
        auto& t = view.get<TransformComponent>(entity);

        // detect external teleport on existing bodies
        if (impl_->entity_to_body.count(entity)) {
            glm::vec3 diff = t.position - rb.last_synced_pos;
            if (glm::dot(diff, diff) > 0.001f) {
                auto body_id = impl_->entity_to_body[entity];
                body_interface.SetPosition(body_id,
                    RVec3(t.position.x, t.position.y, t.position.z),
                    EActivation::Activate);
                body_interface.SetLinearVelocity(body_id, Vec3::sZero());
                body_interface.SetAngularVelocity(body_id, Vec3::sZero());
                rb.last_synced_pos = t.position;
            }
            continue;
        }

        // create shape
        ShapeRefC shape;
        if (rb.shape == RigidBodyComponent::Shape::Box) {
            shape = new BoxShape(Vec3(rb.half_extents.x, rb.half_extents.y, rb.half_extents.z));
        } else {
            shape = new SphereShape(rb.radius);
        }

        EMotionType motion = EMotionType::Dynamic;
        ObjectLayer layer = Layers::MOVING;
        if (rb.type == RigidBodyComponent::Type::Static) {
            motion = EMotionType::Static;
            layer = Layers::NON_MOVING;
        } else if (rb.type == RigidBodyComponent::Type::Kinematic) {
            motion = EMotionType::Kinematic;
        }

        BodyCreationSettings settings(shape,
            RVec3(t.position.x, t.position.y, t.position.z),
            Quat::sIdentity(), motion, layer);

        settings.mRestitution = rb.restitution;
        settings.mFriction = rb.friction;

        BodyID body_id = body_interface.CreateAndAddBody(settings, EActivation::Activate);
        rb.body_id = body_id.GetIndexAndSequenceNumber();

        rb.last_synced_pos = t.position;
        impl_->entity_to_body[entity] = body_id;
        impl_->body_to_entity[rb.body_id] = entity;
    }
}

void PhysicsWorld::update(float dt) {
    impl_->physics_system->SetGravity(Vec3(gravity.x, gravity.y, gravity.z));

    const int steps = 1;
    impl_->physics_system->Update(dt, steps,
        impl_->temp_allocator.get(), impl_->job_system.get());
}

void PhysicsWorld::sync_to_scene(Scene& scene) {
    auto& body_interface = impl_->physics_system->GetBodyInterface();

    for (auto& [entity, body_id] : impl_->entity_to_body) {
        if (!scene.registry().valid(entity)) { continue; }
        if (!scene.registry().all_of<RigidBodyComponent>(entity)) { continue; }

        auto& rb = scene.registry().get<RigidBodyComponent>(entity);
        if (rb.type == RigidBodyComponent::Type::Static) { continue; }

        RVec3 pos = body_interface.GetCenterOfMassPosition(body_id);
        auto& t = scene.registry().get<TransformComponent>(entity);
        t.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
        rb.last_synced_pos = t.position;
    }
}

} // namespace engine
