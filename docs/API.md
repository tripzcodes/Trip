# Trip Engine API Reference

## Core

### Window

```cpp
struct WindowConfig {
    std::string title = "Graphics Engine";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
};

class Window {
    Window(const WindowConfig& config);
    bool should_close() const;
    void poll_events() const;
    VkSurfaceKHR create_surface(VkInstance instance) const;
    GLFWwindow* handle() const;
    uint32_t width() const;
    uint32_t height() const;
    bool was_resized() const;
    void reset_resized();
};
```

### Input

```cpp
class Input {
    Input(GLFWwindow* window);
    void update();                              // call once per frame
    bool key_held(int key) const;               // GLFW key code
    float mouse_dx() const;
    float mouse_dy() const;
    void set_cursor_captured(bool captured);
    bool cursor_captured() const;
};
```

### Camera

```cpp
class Camera {
    Camera(glm::vec3 position = {0, 0, 3});
    void update(const Input& input, float dt);
    glm::mat4 view_matrix() const;
    glm::mat4 projection_matrix(float aspect) const;
    glm::mat4 jittered_projection_matrix(float aspect, glm::vec2 jitter_ndc) const;
    glm::vec3 position() const;
    void set_position(const glm::vec3& p);
    glm::vec3 front() const;
    float yaw() const;
    float pitch() const;
    void set_yaw_pitch(float y, float p);

    float fov = 45.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    float move_speed = 3.0f;
    float look_sensitivity = 0.1f;
};
```

---

## Renderer

### Renderer

The main orchestrator. Owns the full deferred pipeline.

```cpp
class Renderer {
    Renderer(const VulkanContext& context, const Allocator& allocator,
             const Swapchain& swapchain, const std::string& shader_dir);

    void set_scene(Scene* scene);
    VkRenderPass lighting_render_pass() const;

    VkDescriptorSet allocate_material_set(const Texture& albedo, const Texture& normal);
    VkDescriptorSet allocate_material_set(const Texture& albedo);  // default flat normal

    bool begin_frame();
    void render(const Camera& camera, Gui& gui, TextRenderer* text = nullptr);
    void end_frame();

    // toggles
    bool wireframe = false;
    bool show_cascade_debug = false;
    bool frustum_culling = true;
    bool occlusion_culling = false;
    bool taa_enabled = false;
    float taa_sharpness = 0.0f;
    bool gpu_culling = false;
    ShadowMode shadow_mode = ShadowMode::Fixed;
    float shadow_radius = 150.0f;
    float clear_color[3] = {0.02f, 0.02f, 0.02f};
    PostProcessSettings post_settings;

    // stats (read after render)
    uint32_t draw_calls = 0;
    uint32_t culled_objects = 0;
};
```

### Mesh

```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
    glm::vec4 tangent;  // xyz = tangent, w = bitangent sign
};

class Mesh {
    Mesh(const Allocator& allocator,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;
    uint32_t index_count() const;
    VkBuffer vertex_buffer() const;
    VkBuffer index_buffer() const;
    const AABB& bounds() const;
};
```

### Model

```cpp
struct ModelData {
    std::shared_ptr<Mesh> mesh;
    std::string diffuse_texture_path;
    std::string normal_texture_path;
};

class Model {
    static ModelData load_obj(const Allocator& allocator, const std::string& path);
};
```

Computes tangents from UVs. Extracts `map_Kd` (diffuse) and `map_bump` / `map_Kn` (normal) from .mtl.

### Texture

```cpp
class Texture {
    // from file (PNG/JPG/TGA)
    Texture(const VulkanContext& context, const Allocator& allocator,
            const std::string& path, bool linear = false);

    // from raw RGBA pixels
    Texture(const VulkanContext& context, const Allocator& allocator,
            const uint8_t* pixels, uint32_t w, uint32_t h, bool linear = false);

    VkImageView view() const;
    VkSampler sampler() const;
    VkDescriptorSet descriptor_set() const;
    void set_descriptor_set(VkDescriptorSet ds);
};
```

`linear = true` uses `VK_FORMAT_R8G8B8A8_UNORM` for normal maps. Default is `SRGB`.

### ShadowMap

```cpp
enum class ShadowMode { None, Fixed, Cascaded };

constexpr uint32_t SHADOW_MAP_SIZE = 2048;
constexpr uint32_t CASCADE_COUNT = 3;

struct CascadeData {
    glm::mat4 view_proj;
    float split_depth;
};

class ShadowMap {
    ShadowMap(const VulkanContext& context,
              const std::string& vert_path, const std::string& frag_path,
              const std::string& instanced_vert_path = "");

    void compute_fixed(const glm::vec3& light_dir,
                       const glm::vec3& scene_min, const glm::vec3& scene_max);

    void compute_cascaded(const glm::mat4& camera_view, const glm::mat4& camera_proj,
                          const glm::vec3& light_dir, float near, float far,
                          const glm::vec3& scene_min, const glm::vec3& scene_max);

    const CascadeData& cascade(uint32_t i) const;
    VkPipeline pipeline() const;
    VkPipeline instanced_pipeline() const;
};
```

Both modes use texel-snapped ortho projections. PCSS soft shadows in the lighting shader.

### Frustum

```cpp
struct Plane {
    glm::vec3 normal;
    float distance;
    float distance_to(const glm::vec3& point) const;
};

class Frustum {
    void extract(const glm::mat4& vp);                                    // Gribb-Hartmann
    bool test_aabb(const glm::vec3& min, const glm::vec3& max) const;    // true = visible
    const std::array<Plane, 6>& planes() const;
};
```

### InstanceBuffer

```cpp
struct InstanceData {
    glm::mat4 model;
    glm::vec4 albedo;
    glm::vec4 material;  // x = metallic, y = roughness
};

class InstanceBuffer {
    InstanceBuffer(const Allocator& allocator, uint32_t max_instances);
    void reset();                                               // call once per frame
    uint32_t push(const std::vector<InstanceData>& data);       // returns base instance
    void bind(VkCommandBuffer cmd) const;
};
```

### GpuCulling

```cpp
struct GpuEntity {
    glm::mat4 model;
    glm::vec4 aabb_min, aabb_max;
    glm::vec4 albedo, material;
    uint32_t group_id;
};

class GpuCulling {
    GpuCulling(const VulkanContext& context, const Allocator& allocator,
               uint32_t max_entities, const std::string& shader_dir);

    void upload(const std::vector<GpuEntity>& entities,
                const std::vector<GroupInfo>& groups,
                const Frustum& frustum, uint32_t frame_index);

    void dispatch(VkCommandBuffer cmd, uint32_t entity_count, uint32_t frame_index);
    void bind_output_instances(VkCommandBuffer cmd) const;
    VkBuffer indirect_buffer() const;
    uint32_t group_count() const;
};
```

Compute shader frustum test + atomic compaction. Use with `vkCmdDrawIndexedIndirect`.

### TAAPass

```cpp
class TAAPass {
    TAAPass(const VulkanContext& context, const Allocator& allocator,
            uint32_t width, uint32_t height,
            VkImageView hdr_view, VkSampler hdr_sampler,
            VkImageView depth_view, VkSampler depth_sampler,
            const std::string& shader_dir);

    void resolve(VkCommandBuffer cmd, uint32_t frame_index,
                 const Uniforms& uniforms, const PushData& push);
    void swap_history();
    VkImageView resolved_view() const;
    VkSampler resolved_sampler() const;
};
```

Halton jitter, depth reprojection, YCoCg neighborhood clamping, configurable CAS sharpening.

### HiZPyramid

```cpp
class HiZPyramid {
    HiZPyramid(const VulkanContext& context, const Allocator& allocator,
               uint32_t width, uint32_t height,
               VkImageView depth_view, VkSampler depth_sampler,
               const std::string& shader_dir);

    void generate(VkCommandBuffer cmd);                     // build mip pyramid
    void readback(VkCommandBuffer cmd, uint32_t frame);     // copy to staging
    void map_readback(uint32_t frame);                      // map to CPU
    bool test_aabb(const glm::vec3& min, const glm::vec3& max,
                   const glm::mat4& view_proj) const;       // true = visible
    bool has_data() const;
};
```

Double-buffered staging for frame-safe GPU-to-CPU readback.

### PostProcess

```cpp
struct PostProcessSettings {
    bool ssao_enabled = true;
    float ssao_radius = 0.5f;
    float ssao_intensity = 1.5f;
    bool bloom_enabled = true;
    float bloom_threshold = 1.0f;
    float bloom_intensity = 0.3f;
    int tone_map_mode = 2;  // 0=None, 1=Reinhard, 2=ACES
    float exposure = 1.0f;
};
```

### TextRenderer

```cpp
class TextRenderer {
    TextRenderer(const VulkanContext& context, const Allocator& allocator,
                 const Swapchain& swapchain, VkRenderPass render_pass,
                 const std::string& font_path, const std::string& shader_dir,
                 float font_size = 24.0f);

    void draw_text(const std::string& text, float x, float y,
                   const glm::vec3& color = {1}, float scale = 1.0f);
    void render(VkCommandBuffer cmd);
};
```

Screen-space pixel coordinates, origin top-left. Call `draw_text()` each frame, then `render()` inside the post-process render pass.

---

## Scene

### Scene

```cpp
class Scene {
    entt::entity create(const std::string& name = "Entity");
    void destroy(entt::entity entity);
    void clear();
    void set_parent(entt::entity child, entt::entity parent);
    glm::mat4 world_transform(entt::entity entity) const;
    entt::registry& registry();

    template <typename T, typename... Args>
    T& add(entt::entity entity, Args&&... args);

    template <typename T>
    T& get(entt::entity entity);

    template <typename... Components>
    auto view();
};
```

### Components

```cpp
struct TagComponent        { std::string name; };

struct TransformComponent  {
    glm::vec3 position{0};
    glm::vec3 rotation{0};  // euler degrees
    glm::vec3 scale{1};
    glm::mat4 matrix() const;
};

struct MeshComponent       { std::shared_ptr<Mesh> mesh; };

struct MaterialComponent   {
    glm::vec3 albedo{1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    VkDescriptorSet texture_set = VK_NULL_HANDLE;
};

struct BoundsComponent     {
    glm::vec3 min{0}, max{0};
    BoundsComponent transformed(const glm::mat4& m) const;
    static BoundsComponent from_vertices(const std::vector<Vertex>& vertices);
};

struct HierarchyComponent  {
    entt::entity parent{entt::null};
    std::vector<entt::entity> children;
};

struct DirectionalLightComponent {
    glm::vec3 color{1};
    float intensity = 2.0f;
    glm::vec3 ambient_color{0.15f};
    float ambient_intensity = 1.0f;
    static glm::vec3 direction_from_rotation(const glm::vec3& rotation_degrees);
};

struct LODComponent {
    std::vector<LODLevel> levels;
    float cull_distance = 0.0f;
    Mesh* select(float distance) const;
};
```

### SceneSerializer

```cpp
struct SceneSettings { /* camera, renderer, post-process fields */ };

class SceneSerializer {
    static bool save(const Scene& scene, const SceneSettings& settings,
                     const std::string& path);
    static bool load(Scene& scene, SceneSettings& settings,
                     const std::string& path);
};
```

JSON format. Clears scene before loading. Saves all entity components + engine settings.

---

## World

### Terrain

```cpp
struct TerrainConfig {
    float size = 400.0f;
    uint32_t resolution = 128;
    float height_scale = 8.0f;
    float noise_scale = 0.02f;
    int octaves = 4;
    float persistence = 0.5f;
    glm::vec3 offset{0};
};

class Terrain {
    Terrain(const Allocator& allocator, const TerrainConfig& config);
    std::shared_ptr<Mesh> mesh() const;
    const TerrainConfig& config() const;
    float height_at(float world_x, float world_z) const;  // bilinear interpolated
};
```

### ChunkManager

```cpp
using ChunkGenerator = std::function<void(Scene&, const Allocator&, Chunk&, float)>;

class ChunkManager {
    ChunkManager(Scene& scene, const Allocator& allocator,
                 float chunk_size = 32.0f, uint32_t view_radius = 3);

    void set_generator(ChunkGenerator generator);
    void update(const glm::vec3& camera_pos);   // call each frame
    float chunk_size() const;
    uint32_t view_radius() const;
    void set_view_radius(uint32_t r);
    uint32_t loaded_chunks() const;
};
```

Loads chunks in a square around the camera. Hysteresis unloading at `radius + 1`.

---

## Audio

### Audio

```cpp
class Audio {
    Audio();

    uint32_t load(const std::string& path);       // WAV, MP3, FLAC, OGG
    void play(uint32_t handle, bool loop = false);
    void stop(uint32_t handle);
    void set_volume(uint32_t handle, float volume);
    void set_pan(uint32_t handle, float pan);      // -1 left, 0 center, 1 right
    bool is_playing(uint32_t handle) const;

    // 3D spatial audio
    void set_listener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);
    void set_position(uint32_t handle, const glm::vec3& position);
    void set_attenuation(uint32_t handle, float min_dist, float max_dist);

    void set_master_volume(float volume);
    void stop_all();
};
```

`load()` returns a handle. Supports 3D spatialization with linear distance attenuation. Listener should sync to camera position each frame.

---

## Physics

### PhysicsWorld

```cpp
struct RigidBodyComponent {
    enum class Type { Static, Dynamic, Kinematic };
    enum class Shape { Box, Sphere };

    Type type = Type::Dynamic;
    Shape shape = Shape::Box;
    glm::vec3 half_extents{1};
    float radius = 1.0f;
    float mass = 1.0f;
    float restitution = 0.3f;
    float friction = 0.5f;
};

class PhysicsWorld {
    PhysicsWorld();
    void sync_from_scene(Scene& scene);   // create/update bodies from ECS
    void update(float dt);                 // step simulation
    void sync_to_scene(Scene& scene);      // write transforms back to ECS
    glm::vec3 gravity{0, -9.81f, 0};
};
```
