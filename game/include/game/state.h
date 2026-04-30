#pragma once

#include <memory>
#include <vector>

namespace engine {
class Camera;
class Input;
class NavGrid;
class Scene;
}

namespace game {

// Refs that every state can reach. Held by value-of-references in main.cpp,
// passed to update() and the lifecycle hooks each frame.
struct GameContext {
    engine::Scene&   scene;
    engine::Input&   input;
    engine::Camera&  camera;
    engine::NavGrid& navmesh;
    bool&            navmesh_baked;
};

class StateMachine; // forward

class GameState {
public:
    virtual ~GameState() = default;

    virtual void enter(GameContext&, StateMachine&) {}
    virtual void exit (GameContext&, StateMachine&) {}
    virtual void update(GameContext&, StateMachine&, float dt) = 0;
    // ImGui draw call for state-owned overlays (menu title, pause text, ...)
    virtual void render_imgui(GameContext&, StateMachine&) {}

    virtual const char* name() const = 0;

    // when true, the main loop skips world simulation (agents, triggers,
    // physics, day-night). World still RENDERS — the scene just freezes.
    virtual bool pauses_world() const { return true; }
};

class StateMachine {
public:
    void push   (GameContext&, std::unique_ptr<GameState> s);
    void pop    (GameContext&);
    void replace(GameContext&, std::unique_ptr<GameState> s);

    void update      (GameContext&, float dt);
    void render_imgui(GameContext&);

    GameState* top() { return stack_.empty() ? nullptr : stack_.back().get(); }
    bool world_paused() {
        return !stack_.empty() && stack_.back()->pauses_world();
    }

private:
    std::vector<std::unique_ptr<GameState>> stack_;
    // commands buffered during update() so the stack isn't mutated mid-iteration
    enum class Op { None, Push, Pop, Replace };
    Op pending_op_ = Op::None;
    std::unique_ptr<GameState> pending_state_;
};

// concrete states — declared opaquely so callers don't pull in their guts
std::unique_ptr<GameState> make_menu_state();
std::unique_ptr<GameState> make_playing_state();
std::unique_ptr<GameState> make_paused_state();

} // namespace game
