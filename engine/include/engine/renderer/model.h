#pragma once

#include <engine/renderer/mesh.h>

#include <memory>
#include <string>

namespace engine {

class Allocator;

struct ModelData {
    std::shared_ptr<Mesh> mesh;
    std::string diffuse_texture_path; // empty if no .mtl diffuse map
    std::string normal_texture_path;  // empty if no normal/bump map
};

class Model {
public:
    static ModelData load_obj(const Allocator& allocator, const std::string& path);
};

} // namespace engine
