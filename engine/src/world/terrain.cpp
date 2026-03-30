#include <engine/world/terrain.h>
#include <engine/renderer/allocator.h>

#include <cmath>

namespace engine {

// simplex-style noise (value noise with smooth interpolation)
static float hash2d(float x, float y) {
    // fast pseudo-random from 2D coords
    float n = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return n - std::floor(n);
}

static float smooth_noise(float x, float y) {
    float ix = std::floor(x), iy = std::floor(y);
    float fx = x - ix, fy = y - iy;

    // smoothstep interpolation
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);

    float a = hash2d(ix, iy);
    float b = hash2d(ix + 1, iy);
    float c = hash2d(ix, iy + 1);
    float d = hash2d(ix + 1, iy + 1);

    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float fractal_noise(float x, float y, int octaves, float persistence) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float max_val = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += smooth_noise(x * frequency, y * frequency) * amplitude;
        max_val += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / max_val;
}

Terrain::Terrain(const Allocator& allocator, const TerrainConfig& config)
    : config_(config) {
    generate(allocator);
}

float Terrain::height_at(float world_x, float world_z) const {
    uint32_t res = config_.resolution;
    float half = config_.size * 0.5f;

    // world to grid coords
    float gx = (world_x - config_.offset.x + half) / config_.size * (res - 1);
    float gz = (world_z - config_.offset.z + half) / config_.size * (res - 1);

    gx = std::max(0.0f, std::min(gx, static_cast<float>(res - 2)));
    gz = std::max(0.0f, std::min(gz, static_cast<float>(res - 2)));

    uint32_t x0 = static_cast<uint32_t>(gx);
    uint32_t z0 = static_cast<uint32_t>(gz);
    float fx = gx - x0;
    float fz = gz - z0;

    float h00 = heightmap_[z0 * res + x0];
    float h10 = heightmap_[z0 * res + x0 + 1];
    float h01 = heightmap_[(z0 + 1) * res + x0];
    float h11 = heightmap_[(z0 + 1) * res + x0 + 1];

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return config_.offset.y + h0 + (h1 - h0) * fz;
}

void Terrain::generate(const Allocator& allocator) {
    uint32_t res = config_.resolution;
    float half = config_.size * 0.5f;
    float step = config_.size / (res - 1);

    // generate heightmap at full resolution (stored for height_at queries)
    heightmap_.resize(res * res);
    for (uint32_t z = 0; z < res; z++) {
        for (uint32_t x = 0; x < res; x++) {
            float wx = -half + x * step;
            float wz = -half + z * step;
            float h = fractal_noise(
                (wx + config_.offset.x) * config_.noise_scale,
                (wz + config_.offset.z) * config_.noise_scale,
                config_.octaves, config_.persistence);
            heightmap_[z * res + x] = h * config_.height_scale;
        }
    }

    mesh_ = generate_mesh_at(allocator, res);
}

std::shared_ptr<Mesh> Terrain::generate_mesh_at(const Allocator& allocator, uint32_t resolution) const {
    float half = config_.size * 0.5f;
    float step = config_.size / (resolution - 1);

    glm::vec3 color_low{0.35f, 0.45f, 0.28f};
    glm::vec3 color_high{0.55f, 0.50f, 0.40f};

    std::vector<Vertex> vertices(resolution * resolution);
    for (uint32_t z = 0; z < resolution; z++) {
        for (uint32_t x = 0; x < resolution; x++) {
            uint32_t idx = z * resolution + x;
            float wx = config_.offset.x - half + x * step;
            float wz = config_.offset.z - half + z * step;
            float h = height_at(wx, wz) - config_.offset.y;

            vertices[idx].position = {wx, config_.offset.y + h, wz};
            float t = (config_.height_scale > 0.0f) ? h / config_.height_scale : 0.0f;
            vertices[idx].color = glm::mix(color_low, color_high, glm::clamp(t, 0.0f, 1.0f));
            vertices[idx].uv = {
                static_cast<float>(x) / (resolution - 1) * config_.size * 0.1f,
                static_cast<float>(z) / (resolution - 1) * config_.size * 0.1f
            };
        }
    }

    // normals via central differencing
    for (uint32_t z = 0; z < resolution; z++) {
        for (uint32_t x = 0; x < resolution; x++) {
            float wx = config_.offset.x - half + x * step;
            float wz = config_.offset.z - half + z * step;
            float hL = height_at(wx - step, wz) - config_.offset.y;
            float hR = height_at(wx + step, wz) - config_.offset.y;
            float hD = height_at(wx, wz - step) - config_.offset.y;
            float hU = height_at(wx, wz + step) - config_.offset.y;

            glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * step, hD - hU));
            vertices[z * resolution + x].normal = normal;

            glm::vec3 tangent = glm::normalize(glm::vec3(1.0f, (hR - hL) / (2.0f * step), 0.0f));
            tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
            vertices[z * resolution + x].tangent = glm::vec4(tangent, 1.0f);
        }
    }

    // indices (two triangles per grid cell)
    std::vector<uint32_t> indices;
    indices.reserve((resolution - 1) * (resolution - 1) * 6);
    for (uint32_t z = 0; z < resolution - 1; z++) {
        for (uint32_t x = 0; x < resolution - 1; x++) {
            uint32_t tl = z * resolution + x;
            uint32_t tr = tl + 1;
            uint32_t bl = (z + 1) * resolution + x;
            uint32_t br = bl + 1;

            indices.push_back(tl);
            indices.push_back(tr);
            indices.push_back(bl);

            indices.push_back(tr);
            indices.push_back(br);
            indices.push_back(bl);
        }
    }

    return std::make_shared<Mesh>(allocator, vertices, indices);
}

} // namespace engine
