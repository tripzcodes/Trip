#include <game/state.h>

#include <engine/core/input.h>
#include <engine/scene/scene.h>

#include <imgui.h>

namespace game {

// ----------------------------------------------------------------------------
// StateMachine: deferred-mutation stack
// ----------------------------------------------------------------------------

void StateMachine::push(GameContext& ctx, std::unique_ptr<GameState> s) {
    pending_op_ = Op::Push;
    pending_state_ = std::move(s);
    (void)ctx; // applied at top of next update
}

void StateMachine::pop(GameContext&) {
    pending_op_ = Op::Pop;
}

void StateMachine::replace(GameContext&, std::unique_ptr<GameState> s) {
    pending_op_ = Op::Replace;
    pending_state_ = std::move(s);
}

void StateMachine::update(GameContext& ctx, float dt) {
    // apply any pending stack mutation from last frame
    switch (pending_op_) {
        case Op::Push: {
            if (pending_state_) {
                pending_state_->enter(ctx, *this);
                stack_.push_back(std::move(pending_state_));
            }
            break;
        }
        case Op::Pop: {
            if (!stack_.empty()) {
                stack_.back()->exit(ctx, *this);
                stack_.pop_back();
            }
            break;
        }
        case Op::Replace: {
            while (!stack_.empty()) {
                stack_.back()->exit(ctx, *this);
                stack_.pop_back();
            }
            if (pending_state_) {
                pending_state_->enter(ctx, *this);
                stack_.push_back(std::move(pending_state_));
            }
            break;
        }
        case Op::None: break;
    }
    pending_op_ = Op::None;
    pending_state_.reset();

    if (!stack_.empty()) stack_.back()->update(ctx, *this, dt);
}

void StateMachine::render_imgui(GameContext& ctx) {
    if (!stack_.empty()) stack_.back()->render_imgui(ctx, *this);
}

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

namespace {

void centered_overlay(const char* title, const char* hint) {
    auto* vp = ImGui::GetMainViewport();
    ImVec2 size{ 360.0f, 120.0f };
    ImVec2 pos{ vp->Pos.x + (vp->Size.x - size.x) * 0.5f,
                vp->Pos.y + (vp->Size.y - size.y) * 0.5f };

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.85f));

    ImGui::Begin(title, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);

    ImGui::SetCursorPosY(28.0f);
    auto t_size = ImGui::CalcTextSize(title);
    ImGui::SetCursorPosX((size.x - t_size.x) * 0.5f);
    ImGui::TextUnformatted(title);

    ImGui::SetCursorPosY(72.0f);
    auto h_size = ImGui::CalcTextSize(hint);
    ImGui::SetCursorPosX((size.x - h_size.x) * 0.5f);
    ImGui::TextDisabled("%s", hint);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace

// ----------------------------------------------------------------------------
// MenuState — title screen, world frozen, Enter starts game
// ----------------------------------------------------------------------------

namespace {

class MenuState final : public GameState {
public:
    const char* name() const override { return "Menu"; }
    bool pauses_world() const override { return true; }

    void update(GameContext& ctx, StateMachine& sm, float) override {
        if (ctx.input.key_pressed(GLFW_KEY_ENTER)) {
            sm.replace(ctx, make_playing_state());
        }
    }

    void render_imgui(GameContext&, StateMachine&) override {
        centered_overlay("MENU", "press ENTER to start");
    }
};

class PlayingState final : public GameState {
public:
    const char* name() const override { return "Playing"; }
    bool pauses_world() const override { return false; }

    void update(GameContext& ctx, StateMachine& sm, float) override {
        if (ctx.input.key_pressed(GLFW_KEY_P)) {
            sm.push(ctx, make_paused_state());
        }
    }
};

class PausedState final : public GameState {
public:
    const char* name() const override { return "Paused"; }
    bool pauses_world() const override { return true; }

    void update(GameContext& ctx, StateMachine& sm, float) override {
        if (ctx.input.key_pressed(GLFW_KEY_P)) {
            sm.pop(ctx);
        }
        if (ctx.input.key_pressed(GLFW_KEY_M)) {
            sm.replace(ctx, make_menu_state());
        }
    }

    void render_imgui(GameContext&, StateMachine&) override {
        centered_overlay("PAUSED", "P resume   M menu");
    }
};

} // namespace

std::unique_ptr<GameState> make_menu_state()    { return std::make_unique<MenuState>(); }
std::unique_ptr<GameState> make_playing_state() { return std::make_unique<PlayingState>(); }
std::unique_ptr<GameState> make_paused_state()  { return std::make_unique<PausedState>(); }

} // namespace game
