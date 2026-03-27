#include <engine/animation/gltf_loader.h>
#include <engine/renderer/allocator.h>

#include <cgltf.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace engine {

static std::string base_dir(const std::string& path) {
    auto pos = path.find_last_of('/');
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

static const float* accessor_float(const cgltf_accessor* acc, cgltf_size idx, cgltf_size comp) {
    static float buf[16];
    cgltf_accessor_read_float(acc, idx, buf, comp);
    return buf;
}

GltfModel GltfLoader::load(const Allocator& allocator, const std::string& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;

    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
        throw std::runtime_error("Failed to parse glTF: " + path);

    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        throw std::runtime_error("Failed to load glTF buffers: " + path);
    }

    GltfModel result;
    std::string dir = base_dir(path);

    // find the first mesh with a skin
    cgltf_mesh* gltf_mesh = nullptr;
    cgltf_skin* gltf_skin = nullptr;

    for (cgltf_size n = 0; n < data->nodes_count; n++) {
        if (data->nodes[n].mesh && data->nodes[n].skin) {
            gltf_mesh = data->nodes[n].mesh;
            gltf_skin = data->nodes[n].skin;
            break;
        }
    }

    if (!gltf_mesh) {
        // no skinned mesh — try loading as static
        if (data->meshes_count > 0) {
            gltf_mesh = &data->meshes[0];
        }
        if (!gltf_mesh || gltf_mesh->primitives_count == 0) {
            cgltf_free(data);
            throw std::runtime_error("No mesh found in glTF: " + path);
        }
    }

    // parse first primitive
    cgltf_primitive& prim = gltf_mesh->primitives[0];

    cgltf_accessor* pos_acc = nullptr;
    cgltf_accessor* norm_acc = nullptr;
    cgltf_accessor* uv_acc = nullptr;
    cgltf_accessor* tang_acc = nullptr;
    cgltf_accessor* joints_acc = nullptr;
    cgltf_accessor* weights_acc = nullptr;

    for (cgltf_size a = 0; a < prim.attributes_count; a++) {
        auto& attr = prim.attributes[a];
        if (attr.type == cgltf_attribute_type_position) pos_acc = attr.data;
        else if (attr.type == cgltf_attribute_type_normal) norm_acc = attr.data;
        else if (attr.type == cgltf_attribute_type_texcoord) uv_acc = attr.data;
        else if (attr.type == cgltf_attribute_type_tangent) tang_acc = attr.data;
        else if (attr.type == cgltf_attribute_type_joints) joints_acc = attr.data;
        else if (attr.type == cgltf_attribute_type_weights) weights_acc = attr.data;
    }

    if (!pos_acc) {
        cgltf_free(data);
        throw std::runtime_error("glTF mesh has no position attribute");
    }

    cgltf_size vertex_count = pos_acc->count;
    bool is_skinned = (joints_acc && weights_acc && gltf_skin);

    // build vertices
    std::vector<SkinnedVertex> skinned_verts;
    std::vector<Vertex> static_verts;

    if (is_skinned) skinned_verts.resize(vertex_count);
    else static_verts.resize(vertex_count);

    for (cgltf_size i = 0; i < vertex_count; i++) {
        float buf[4];

        glm::vec3 pos{0};
        cgltf_accessor_read_float(pos_acc, i, &pos.x, 3);

        glm::vec3 norm{0, 1, 0};
        if (norm_acc) cgltf_accessor_read_float(norm_acc, i, &norm.x, 3);

        glm::vec2 uv{0};
        if (uv_acc) cgltf_accessor_read_float(uv_acc, i, &uv.x, 2);

        glm::vec4 tang{1, 0, 0, 1};
        if (tang_acc) cgltf_accessor_read_float(tang_acc, i, &tang.x, 4);

        if (is_skinned) {
            auto& v = skinned_verts[i];
            v.position = pos;
            v.normal = norm;
            v.color = glm::vec3(0.8f);
            v.uv = uv;
            v.tangent = tang;

            cgltf_uint joints[4] = {0};
            cgltf_accessor_read_uint(joints_acc, i, joints, 4);
            v.bone_indices = {joints[0], joints[1], joints[2], joints[3]};

            cgltf_accessor_read_float(weights_acc, i, buf, 4);
            v.bone_weights = {buf[0], buf[1], buf[2], buf[3]};
        } else {
            auto& v = static_verts[i];
            v.position = pos;
            v.normal = norm;
            v.color = glm::vec3(0.8f);
            v.uv = uv;
            v.tangent = tang;
        }
    }

    // indices
    std::vector<uint32_t> indices;
    if (prim.indices) {
        indices.resize(prim.indices->count);
        for (cgltf_size i = 0; i < prim.indices->count; i++) {
            indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
        }
    } else {
        indices.resize(vertex_count);
        for (uint32_t i = 0; i < vertex_count; i++) indices[i] = i;
    }

    // create mesh
    if (is_skinned) {
        result.skinned_mesh = std::make_shared<SkinnedMesh>(allocator, skinned_verts, indices);
    } else {
        result.static_mesh = std::make_shared<Mesh>(allocator, static_verts, indices);
    }

    // parse skeleton
    if (gltf_skin) {
        result.skeleton.bones.resize(gltf_skin->joints_count);

        // inverse bind matrices
        if (gltf_skin->inverse_bind_matrices) {
            for (cgltf_size i = 0; i < gltf_skin->joints_count; i++) {
                float m[16];
                cgltf_accessor_read_float(gltf_skin->inverse_bind_matrices, i, m, 16);
                memcpy(&result.skeleton.bones[i].inverse_bind_matrix, m, 64);
            }
        }

        // bone hierarchy
        for (cgltf_size i = 0; i < gltf_skin->joints_count; i++) {
            cgltf_node* joint = gltf_skin->joints[i];
            auto& bone = result.skeleton.bones[i];
            bone.name = joint->name ? joint->name : ("bone_" + std::to_string(i));
            result.skeleton.bone_name_to_index[bone.name] = static_cast<uint32_t>(i);

            // find parent
            bone.parent_index = -1;
            if (joint->parent) {
                for (cgltf_size j = 0; j < gltf_skin->joints_count; j++) {
                    if (gltf_skin->joints[j] == joint->parent) {
                        bone.parent_index = static_cast<int32_t>(j);
                        break;
                    }
                }
            }

            // local bind transform
            if (joint->has_matrix) {
                memcpy(&bone.local_bind_transform, joint->matrix, 64);
            } else {
                glm::vec3 t{0}; glm::quat r{1, 0, 0, 0}; glm::vec3 s{1};
                if (joint->has_translation) t = {joint->translation[0], joint->translation[1], joint->translation[2]};
                if (joint->has_rotation) r = {joint->rotation[3], joint->rotation[0], joint->rotation[1], joint->rotation[2]};
                if (joint->has_scale) s = {joint->scale[0], joint->scale[1], joint->scale[2]};

                glm::mat4 mt = glm::translate(glm::mat4(1), t);
                glm::mat4 mr = glm::mat4_cast(r);
                glm::mat4 ms = glm::scale(glm::mat4(1), s);
                bone.local_bind_transform = mt * mr * ms;
            }
        }
    }

    // parse animations
    for (cgltf_size a = 0; a < data->animations_count; a++) {
        cgltf_animation& ganim = data->animations[a];
        AnimationClip clip;
        clip.name = ganim.name ? ganim.name : ("anim_" + std::to_string(a));
        clip.duration = 0.0f;

        for (cgltf_size c = 0; c < ganim.channels_count; c++) {
            cgltf_animation_channel& gchan = ganim.channels[c];
            cgltf_animation_sampler& gsamp = *gchan.sampler;

            // find bone index
            if (!gchan.target_node) continue;
            uint32_t bone_idx = UINT32_MAX;
            if (gltf_skin) {
                for (cgltf_size j = 0; j < gltf_skin->joints_count; j++) {
                    if (gltf_skin->joints[j] == gchan.target_node) {
                        bone_idx = static_cast<uint32_t>(j);
                        break;
                    }
                }
            }
            if (bone_idx == UINT32_MAX) continue;

            // find or create channel
            BoneChannel* ch = nullptr;
            for (auto& existing : clip.channels) {
                if (existing.bone_index == bone_idx) { ch = &existing; break; }
            }
            if (!ch) {
                clip.channels.push_back({bone_idx, {}, {}, {}});
                ch = &clip.channels.back();
            }

            // read keyframe times
            cgltf_size key_count = gsamp.input->count;
            std::vector<float> times(key_count);
            for (cgltf_size k = 0; k < key_count; k++) {
                cgltf_accessor_read_float(gsamp.input, k, &times[k], 1);
                clip.duration = std::max(clip.duration, times[k]);
            }

            // read keyframe values
            for (cgltf_size k = 0; k < key_count; k++) {
                float val[4];
                cgltf_size num_comp = cgltf_num_components(gsamp.output->type);
                cgltf_accessor_read_float(gsamp.output, k, val, num_comp);

                if (gchan.target_path == cgltf_animation_path_type_translation) {
                    ch->positions.push_back({times[k], {val[0], val[1], val[2]}});
                } else if (gchan.target_path == cgltf_animation_path_type_rotation) {
                    ch->rotations.push_back({times[k], glm::quat(val[3], val[0], val[1], val[2])});
                } else if (gchan.target_path == cgltf_animation_path_type_scale) {
                    ch->scales.push_back({times[k], {val[0], val[1], val[2]}});
                }
            }
        }

        result.animations.push_back(std::move(clip));
    }

    // textures
    if (prim.material) {
        if (prim.material->pbr_metallic_roughness.base_color_texture.texture) {
            auto* img = prim.material->pbr_metallic_roughness.base_color_texture.texture->image;
            if (img && img->uri) result.diffuse_texture_path = dir + img->uri;
        }
        if (prim.material->normal_texture.texture) {
            auto* img = prim.material->normal_texture.texture->image;
            if (img && img->uri) result.normal_texture_path = dir + img->uri;
        }
    }

    std::cout << "[engine] Loaded glTF " << path << ": "
              << vertex_count << " vertices, "
              << indices.size() / 3 << " triangles, "
              << result.skeleton.bone_count() << " bones, "
              << result.animations.size() << " animations\n";

    cgltf_free(data);
    return result;
}

} // namespace engine
