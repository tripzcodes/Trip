#pragma once

#include <engine/animation/skeleton.h>
#include <engine/animation/animation_clip.h>

#include <glm/glm.hpp>

#include <vector>

namespace engine {

class AnimationPlayer {
public:
    void set_skeleton(const Skeleton* skeleton) { skeleton_ = skeleton; }
    void play(const AnimationClip* clip, bool loop = true);
    void stop();
    void update(float dt);

    const std::vector<glm::mat4>& bone_matrices() const { return final_matrices_; }
    bool is_playing() const { return playing_; }
    float current_time() const { return current_time_; }

private:
    glm::vec3 sample_position(const BoneChannel& ch, float time) const;
    glm::quat sample_rotation(const BoneChannel& ch, float time) const;
    glm::vec3 sample_scale(const BoneChannel& ch, float time) const;

    const AnimationClip* clip_ = nullptr;
    const Skeleton* skeleton_ = nullptr;
    float current_time_ = 0.0f;
    bool playing_ = false;
    bool loop_ = true;
    std::vector<glm::mat4> final_matrices_;
};

} // namespace engine
