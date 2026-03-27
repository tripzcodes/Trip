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
        uint32_t n = skeleton_->bone_count();
        final_matrices_.resize(n, glm::mat4(1.0f));
        local_transforms_.resize(n);
        world_transforms_.resize(n);
    }
}

void AnimationPlayer::stop() {
    playing_ = false;
    current_time_ = 0.0f;
}

template <typename Key>
static uint32_t find_key(const std::vector<Key>& keys, float time) {
    if (keys.size() <= 2) {
        return 0;
    }
    // binary search for the interval containing time
    uint32_t lo = 0, hi = static_cast<uint32_t>(keys.size() - 2);
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1) / 2;
        if (keys[mid].time <= time) lo = mid;
        else hi = mid - 1;
    }
    return lo;
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

    // reuse pre-allocated buffers
    for (uint32_t i = 0; i < bone_count; i++) {
        local_transforms_[i] = skeleton_->bones[i].local_bind_transform;
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
        local_transforms_[ch.bone_index] = t * r * s;
    }

    // compute world transforms (parent-first traversal)
    for (uint32_t i = 0; i < bone_count; i++) {
        int32_t parent = skeleton_->bones[i].parent_index;
        if (parent >= 0) {
            world_transforms_[i] = world_transforms_[parent] * local_transforms_[i];
        } else {
            world_transforms_[i] = local_transforms_[i];
        }
        final_matrices_[i] = world_transforms_[i] * skeleton_->bones[i].inverse_bind_matrix;
    }
}

} // namespace engine
