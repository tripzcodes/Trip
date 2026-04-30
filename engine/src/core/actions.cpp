#include <engine/core/actions.h>
#include <engine/core/input.h>

#include <algorithm>

namespace engine {

const std::vector<int> ActionMap::empty_{};

void ActionMap::bind(const std::string& action, int glfw_key) {
    auto& keys = bindings_[action];
    if (std::find(keys.begin(), keys.end(), glfw_key) == keys.end()) {
        keys.push_back(glfw_key);
    }
    if (std::find(order_.begin(), order_.end(), action) == order_.end()) {
        order_.push_back(action);
    }
}

void ActionMap::unbind(const std::string& action, int glfw_key) {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) return;
    auto& keys = it->second;
    keys.erase(std::remove(keys.begin(), keys.end(), glfw_key), keys.end());
}

void ActionMap::clear(const std::string& action) {
    bindings_.erase(action);
    order_.erase(std::remove(order_.begin(), order_.end(), action), order_.end());
}

bool ActionMap::held(const Input& input, const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) return false;
    for (int k : it->second) if (input.key_held(k)) return true;
    return false;
}

bool ActionMap::pressed(const Input& input, const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) return false;
    for (int k : it->second) if (input.key_pressed(k)) return true;
    return false;
}

bool ActionMap::released(const Input& input, const std::string& action) const {
    auto it = bindings_.find(action);
    if (it == bindings_.end()) return false;
    for (int k : it->second) if (input.key_released(k)) return true;
    return false;
}

const std::vector<int>& ActionMap::keys_for(const std::string& action) const {
    auto it = bindings_.find(action);
    return it == bindings_.end() ? empty_ : it->second;
}

} // namespace engine
