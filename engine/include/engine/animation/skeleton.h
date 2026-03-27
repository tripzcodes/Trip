#pragma once

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

struct Bone {
    std::string name;
    int32_t parent_index = -1;
    glm::mat4 inverse_bind_matrix{1.0f};
    glm::mat4 local_bind_transform{1.0f};
};

struct Skeleton {
    std::vector<Bone> bones;
    std::unordered_map<std::string, uint32_t> bone_name_to_index;

    uint32_t bone_count() const { return static_cast<uint32_t>(bones.size()); }
};

} // namespace engine
