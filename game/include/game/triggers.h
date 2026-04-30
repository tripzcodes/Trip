#pragma once

namespace engine { class Scene; }

namespace game {

// Process every TriggerComponent: fire its action on agents that just entered
// the volume, then destroy any entity whose HealthComponent has reached zero.
// Call once per frame, after agents have been advanced.
void update_triggers(engine::Scene& scene);

} // namespace game
