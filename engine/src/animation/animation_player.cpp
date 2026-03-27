#include <engine/animation/animation_player.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>

namespace engine {

void AnimationPlayer::play(const AnimationClip* clip, bool loop) {
    clip_ = clip;
    loop_ = loop;
    current_time_ = 0.0f;
    playing_ = true;
    if (skeleton_) {
        final_matrices_.resize(skeleton_->bone_count(), glm::mat4(1.0f));
    }
}

void AnimationPlayer::stop() {
    playing_ = false;
    current_time_ = 0.0f;
}

template <typename Key>
static uint32_t find_key(const std::vector<Key>& keys, float time) {
    for (uint32_t i = 0; i + 1 < keys.size(); i++) {
        if (time < keys[i + 1].time) return i;
    }
    return keys.empty() ? 0 : static_cast<uint32_t>(keys.size() - 2);
}

glm::vec3 AnimationPlayer::sample_position(const BoneChannel& ch, float time) const {
    if (ch.positions.size() == 1) return ch.positions[0].value;
    uint32_t i = find_key(ch.positions, time);
    float t = (time - ch.positions[i].time) /
              (ch.positions[i + 1].time - ch.positions[i].time);
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::mix(ch.positions[i].value, ch.positions[i + 1].value, t);
}

glm::quat AnimationPlayer::sample_rotation(const BoneChannel& ch, float time) const {
    if (ch.rotations.size() == 1) return ch.rotations[0].value;
    uint32_t i = find_key(ch.rotations, time);
    float t = (time - ch.rotations[i].time) /
              (ch.rotations[i + 1].time - ch.rotations[i].time);
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::slerp(ch.rotations[i].value, ch.rotations[i + 1].value, t);
}

glm::vec3 AnimationPlayer::sample_scale(const BoneChannel& ch, float time) const {
    if (ch.scales.size() == 1) return ch.scales[0].value;
    uint32_t i = find_key(ch.scales, time);
    float t = (time - ch.scales[i].time) /
              (ch.scales[i + 1].time - ch.scales[i].time);
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::mix(ch.scales[i].value, ch.scales[i + 1].value, t);
}

void AnimationPlayer::update(float dt) {
    if (!playing_ || !clip_ || !skeleton_) return;

    current_time_ += dt;
    if (current_time_ > clip_->duration) {
        if (loop_) {
            current_time_ = std::fmod(current_time_, clip_->duration);
        } else {
            current_time_ = clip_->duration;
            playing_ = false;
        }
    }

    uint32_t bone_count = skeleton_->bone_count();
    final_matrices_.resize(bone_count);

    // build per-bone local transforms from animation channels
    std::vector<glm::mat4> local_transforms(bone_count);
    for (uint32_t i = 0; i < bone_count; i++) {
        local_transforms[i] = skeleton_->bones[i].local_bind_transform;
    }

    for (const auto& ch : clip_->channels) {
        if (ch.bone_index >= bone_count) continue;

        glm::vec3 pos = ch.positions.empty()
            ? glm::vec3(0.0f) : sample_position(ch, current_time_);
        glm::quat rot = ch.rotations.empty()
            ? glm::quat(1, 0, 0, 0) : sample_rotation(ch, current_time_);
        glm::vec3 scl = ch.scales.empty()
            ? glm::vec3(1.0f) : sample_scale(ch, current_time_);

        glm::mat4 t = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 r = glm::toMat4(rot);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scl);
        local_transforms[ch.bone_index] = t * r * s;
    }

    // compute world transforms (parent-first traversal)
    std::vector<glm::mat4> world_transforms(bone_count);
    for (uint32_t i = 0; i < bone_count; i++) {
        int32_t parent = skeleton_->bones[i].parent_index;
        if (parent >= 0) {
            world_transforms[i] = world_transforms[parent] * local_transforms[i];
        } else {
            world_transforms[i] = local_transforms[i];
        }
        final_matrices_[i] = world_transforms[i] * skeleton_->bones[i].inverse_bind_matrix;
    }
}

} // namespace engine
