#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class Input;

// Maps named actions ("jump", "fire", "pause") to one or more GLFW keys.
// Lets game code query intents instead of physical keys, so rebinding and
// future controller support are local changes. Multiple keys may bind to
// the same action (e.g. WASD + arrow keys both → "move_forward").
class ActionMap {
public:
    void bind(const std::string& action, int glfw_key);
    void unbind(const std::string& action, int glfw_key);
    void clear(const std::string& action);

    bool held    (const Input& input, const std::string& action) const;
    bool pressed (const Input& input, const std::string& action) const;
    bool released(const Input& input, const std::string& action) const;

    // for menus / debug UI: iterate registered action names in insertion order
    const std::vector<std::string>& actions() const { return order_; }
    const std::vector<int>& keys_for(const std::string& action) const;

private:
    std::unordered_map<std::string, std::vector<int>> bindings_;
    std::vector<std::string> order_;
    static const std::vector<int> empty_;
};

} // namespace engine
