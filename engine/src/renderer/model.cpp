#include <engine/renderer/model.h>
#include <engine/renderer/allocator.h>

#include <tiny_obj_loader.h>

#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace engine {

struct VertexHash {
    size_t operator()(const Vertex& v) const {
        size_t h = 0;
        auto hash_combine = [&h](float f) {
            h ^= std::hash<float>{}(f) + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        hash_combine(v.position.x);
        hash_combine(v.position.y);
        hash_combine(v.position.z);
        hash_combine(v.normal.x);
        hash_combine(v.normal.y);
        hash_combine(v.normal.z);
        hash_combine(v.uv.x);
        hash_combine(v.uv.y);
        return h;
    }
};

struct VertexEqual {
    bool operator()(const Vertex& a, const Vertex& b) const {
        return a.position == b.position &&
               a.normal == b.normal &&
               a.uv == b.uv;
    }
};

ModelData Model::load_obj(const Allocator& allocator, const std::string& path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string base_dir = path.substr(0, path.find_last_of('/') + 1);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                          path.c_str(), base_dir.c_str())) {
        throw std::runtime_error("Failed to load OBJ: " + path + " " + err);
    }

    if (!warn.empty()) {
        std::cerr << "[obj] " << warn << "\n";
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::unordered_map<Vertex, uint32_t, VertexHash, VertexEqual> unique_vertices;

    bool has_normals = !attrib.normals.empty();

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            vertex.position = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            if (has_normals && index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }

            if (index.texcoord_index >= 0) {
                vertex.uv = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };
            }

            vertex.color = {0.8f, 0.8f, 0.8f};

            if (unique_vertices.count(vertex) == 0) {
                unique_vertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(unique_vertices[vertex]);
        }
    }

    // compute smooth normals if the OBJ had none
    if (!has_normals) {
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            auto& v0 = vertices[indices[i + 0]];
            auto& v1 = vertices[indices[i + 1]];
            auto& v2 = vertices[indices[i + 2]];

            glm::vec3 edge1 = v1.position - v0.position;
            glm::vec3 edge2 = v2.position - v0.position;
            glm::vec3 face_normal = glm::cross(edge1, edge2);

            v0.normal += face_normal;
            v1.normal += face_normal;
            v2.normal += face_normal;
        }

        for (auto& v : vertices) {
            if (glm::length(v.normal) > 0.0f) {
                v.normal = glm::normalize(v.normal);
            }
        }
    }

    std::cout << "[engine] Loaded " << path << ": "
              << vertices.size() << " vertices, "
              << indices.size() / 3 << " triangles\n";

    ModelData result;
    result.mesh = std::make_shared<Mesh>(allocator, vertices, indices);
    if (!materials.empty() && !materials[0].diffuse_texname.empty()) {
        result.diffuse_texture_path = base_dir + materials[0].diffuse_texname;
    }
    return result;
}

} // namespace engine
