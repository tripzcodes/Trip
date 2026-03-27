#pragma once

#include <engine/renderer/skinned_mesh.h>
#include <engine/renderer/mesh.h>
#include <engine/animation/skeleton.h>
#include <engine/animation/animation_clip.h>

#include <memory>
#include <string>
#include <vector>

namespace engine {

class Allocator;

struct GltfModel {
    std::shared_ptr<SkinnedMesh> skinned_mesh;
    std::shared_ptr<Mesh> static_mesh;
    Skeleton skeleton;
    std::vector<AnimationClip> animations;
    std::string diffuse_texture_path;
    std::string normal_texture_path;
};

class GltfLoader {
public:
    static GltfModel load(const Allocator& allocator, const std::string& path);
};

} // namespace engine
