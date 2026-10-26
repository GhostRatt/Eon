#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "ufbx.h"

#include "CharacterAsset.h"

namespace eon {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Normalize common rig naming: "DEF-breast.L" / "Breast_L" / "mixamorig:LeftBreast" -> comparable keys
std::string NormalizeBoneName(const std::string& n) {
    std::string s = Lower(n);
    for (const char* prefix : {"mixamorig:", "def-", "def_", "c_", "bip01 ", "bip001 "}) {
        if (s.rfind(prefix, 0) == 0) s = s.substr(std::char_traits<char>::length(prefix));
    }
    std::string o;
    for (char c : s) if (std::isalnum(static_cast<unsigned char>(c))) o += c;
    // trailing side markers: "breastl", ".l" -> "l"
    return o;
}

Mat4 ToMat(const ufbx_matrix& m) {
    Mat4 o;
    o.m[0] = (float)m.cols[0].x; o.m[1] = (float)m.cols[0].y; o.m[2] = (float)m.cols[0].z; o.m[3] = 0;
    o.m[4] = (float)m.cols[1].x; o.m[5] = (float)m.cols[1].y; o.m[6] = (float)m.cols[1].z; o.m[7] = 0;
    o.m[8] = (float)m.cols[2].x; o.m[9] = (float)m.cols[2].y; o.m[10] = (float)m.cols[2].z; o.m[11] = 0;
    o.m[12] = (float)m.cols[3].x; o.m[13] = (float)m.cols[3].y; o.m[14] = (float)m.cols[3].z; o.m[15] = 1;
    return o;
}

Transform ToTransform(const ufbx_matrix& m) {
    const ufbx_transform t = ufbx_matrix_to_transform(&m);
    Transform o;
    o.t = {(float)t.translation.x, (float)t.translation.y, (float)t.translation.z};
    o.r = Quat{(float)t.rotation.x, (float)t.rotation.y, (float)t.rotation.z, (float)t.rotation.w}.Normalized();
    o.s = {(float)t.scale.x, (float)t.scale.y, (float)t.scale.z};
    return o;
}

Vec3 V(const ufbx_vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }

std::string Str(const ufbx_string& s) { return std::string(s.data, s.length); }

std::string TexPath(const ufbx_material_map& map) {
    if (!map.texture) return {};
    const ufbx_texture* t = map.texture;
    if (t->relative_filename.length) return Str(t->relative_filename);
    if (t->filename.length) return Str(t->filename);
    return Str(t->absolute_filename);
}

struct VertKey {
    uint32_t v, n, uv;
    bool operator==(const VertKey& o) const { return v == o.v && n == o.n && uv == o.uv; }
};
struct VertKeyHash {
    size_t operator()(const VertKey& k) const { return (size_t(k.v) * 73856093u) ^ (size_t(k.n) * 19349663u) ^ (size_t(k.uv) * 83492791u); }
};

} // namespace

int CharacterAsset::FindBone(const std::string& name) const {
    for (int i = 0; i < (int)bones.size(); ++i) if (bones[i].name == name) return i;
    return -1;
}

int CharacterAsset::FindBoneAny(std::initializer_list<const char*> candidates) const {
    for (const char* c : candidates) {
        const std::string want = NormalizeBoneName(c);
        for (int i = 0; i < (int)bones.size(); ++i)
            if (NormalizeBoneName(bones[i].name) == want) return i;
    }
    return -1;
}

size_t CharacterAsset::VertexCount() const {
    size_t n = 0;
    for (const SkinMesh& m : meshes) n += m.pos.size();
    return n;
}

size_t CharacterAsset::TriangleCount() const {
    size_t n = 0;
    for (const SkinMesh& m : meshes) n += m.indices.size() / 3;
    return n;
}

bool ImportFbx(const std::string& path, CharacterAsset& out, const ImportOptions& opt, std::string* error) {
    ufbx_load_opts lo = {};
    lo.target_axes = ufbx_axes_right_handed_y_up;
    lo.target_unit_meters = 1.0f;
    lo.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    lo.generate_missing_normals = true;

    ufbx_error err;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &lo, &err);
    if (!scene) {
        if (error) *error = std::string(err.description.data, err.description.length);
        return false;
    }

    CharacterAsset a;
    a.sourcePath = path;

    // ---- Bones: every skin cluster's bone node plus its ancestors ----
    std::unordered_set<const ufbx_node*> boneNodes;
    std::unordered_map<const ufbx_node*, ufbx_matrix> bindWorld;
    for (const ufbx_mesh* mesh : scene->meshes) {
        for (const ufbx_skin_deformer* skin : mesh->skin_deformers) {
            for (const ufbx_skin_cluster* c : skin->clusters) {
                if (!c->bone_node) continue;
                bindWorld.emplace(c->bone_node, c->bind_to_world);
                for (const ufbx_node* n = c->bone_node; n && !n->is_root; n = n->parent) boneNodes.insert(n);
            }
        }
    }
    // Also take plain skeleton bones (e.g. extra soft bones with no weights yet)
    for (const ufbx_node* n : scene->nodes) {
        if (n->bone && !n->is_root) for (const ufbx_node* p = n; p && !p->is_root; p = p->parent) boneNodes.insert(p);
    }

    std::unordered_map<const ufbx_node*, int> boneIndex;
    std::function<void(const ufbx_node*, int)> visit = [&](const ufbx_node* n, int parent) {
        int self = parent;
        if (boneNodes.count(n)) {
            Bone b;
            b.name = Str(n->name);
            b.parent = parent;
            auto it = bindWorld.find(n);
            b.bindWorld = ToMat(it != bindWorld.end() ? it->second : n->node_to_world);
            self = static_cast<int>(a.bones.size());
            boneIndex[n] = self;
            a.bones.push_back(b);
        }
        for (const ufbx_node* c : n->children) visit(c, self);
    };
    visit(scene->root_node, -1);

    if (a.bones.empty()) {   // static mesh: one root bone
        Bone b;
        b.name = "root";
        a.bones.push_back(b);
    }

    // Bind locals and inverse binds from world binds
    std::vector<Transform> bindWorldT(a.bones.size());
    for (size_t i = 0; i < a.bones.size(); ++i) {
        const Mat4& m = a.bones[i].bindWorld;
        ufbx_matrix um;
        for (int c = 0; c < 4; ++c) um.cols[c] = {m.m[c * 4 + 0], m.m[c * 4 + 1], m.m[c * 4 + 2]};
        bindWorldT[i] = ToTransform(um);
        a.bones[i].invBind = Mat4::FromTransform(bindWorldT[i].Inverse());
    }
    for (size_t i = 0; i < a.bones.size(); ++i) {
        const int p = a.bones[i].parent;
        a.bones[i].bindLocal = p >= 0 ? bindWorldT[p].Inverse() * bindWorldT[i] : bindWorldT[i];
    }

    // ---- Materials ----
    std::unordered_map<const ufbx_material*, int> matIndex;
    auto materialOf = [&](const ufbx_material* m) -> int {
        if (!m) return -1;
        auto it = matIndex.find(m);
        if (it != matIndex.end()) return it->second;
        MaterialInfo mi;
        mi.name = Str(m->name);
        const ufbx_material_map& base = m->pbr.base_color.texture ? m->pbr.base_color : m->fbx.diffuse_color;
        mi.baseColorTex = TexPath(base);
        if (base.has_value) mi.baseColor = {(float)base.value_vec3.x, (float)base.value_vec3.y, (float)base.value_vec3.z};
        mi.normalTex = TexPath(m->pbr.normal_map.texture ? m->pbr.normal_map : m->fbx.normal_map);
        mi.roughnessTex = TexPath(m->pbr.roughness);
        const int idx = static_cast<int>(a.materials.size());
        a.materials.push_back(mi);
        matIndex[m] = idx;
        return idx;
    };

    // ---- Meshes ----
    std::vector<uint32_t> tri;
    for (const ufbx_node* node : scene->nodes) {
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->num_faces == 0) continue;
        tri.resize(mesh->max_face_triangles * 3);

        const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        const ufbx_matrix& g2w = node->geometry_to_world;

        // Rigid meshes follow their closest bone ancestor
        int rigidBone = 0;
        for (const ufbx_node* p = node; p; p = p->parent) {
            auto it = boneIndex.find(p);
            if (it != boneIndex.end()) { rigidBone = it->second; break; }
        }

        // Per FBX vertex: top 4 weights
        std::vector<std::array<uint16_t, 4>> vJoints(mesh->num_vertices, {static_cast<uint16_t>(rigidBone), 0, 0, 0});
        std::vector<std::array<float, 4>> vWeights(mesh->num_vertices, {1.f, 0, 0, 0});
        if (skin) {
            for (size_t v = 0; v < mesh->num_vertices && v < skin->vertices.count; ++v) {
                const ufbx_skin_vertex& sv = skin->vertices.data[v];
                std::array<uint16_t, 4> j{};
                std::array<float, 4> w{};
                float total = 0.f;
                for (uint32_t k = 0; k < sv.num_weights && k < 4; ++k) {
                    const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + k];
                    const ufbx_skin_cluster* c = skin->clusters.data[sw.cluster_index];
                    auto it = c->bone_node ? boneIndex.find(c->bone_node) : boneIndex.end();
                    j[k] = static_cast<uint16_t>(it != boneIndex.end() ? it->second : rigidBone);
                    w[k] = static_cast<float>(sw.weight);
                    total += w[k];
                }
                if (total > 1e-6f) {
                    for (float& x : w) x /= total;
                    vJoints[v] = j;
                    vWeights[v] = w;
                }
            }
        }

        // One SkinMesh per material used by this mesh
        std::unordered_map<int, size_t> groupOf;        // material slot -> out mesh index
        std::unordered_map<int, std::unordered_map<VertKey, uint32_t, VertKeyHash>> dedupe;
        std::unordered_map<int, std::vector<std::vector<uint32_t>>> fbxToOut;

        for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            const int slot = mesh->face_material.count ? static_cast<int>(mesh->face_material.data[fi]) : 0;
            auto git = groupOf.find(slot);
            if (git == groupOf.end()) {
                SkinMesh sm;
                sm.name = Str(node->name);
                const ufbx_material* mat = slot < (int)mesh->materials.count ? mesh->materials.data[slot] : nullptr;
                sm.material = materialOf(mat);
                if (sm.material >= 0) sm.name += "/" + a.materials[sm.material].name;
                git = groupOf.emplace(slot, a.meshes.size()).first;
                a.meshes.push_back(std::move(sm));
                fbxToOut[slot].resize(mesh->num_vertices);
            }
            SkinMesh& sm = a.meshes[git->second];
            auto& map = dedupe[slot];
            auto& back = fbxToOut[slot];

            const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t k = 0; k < n * 3; ++k) {
                const uint32_t ix = tri[k];
                const uint32_t v = mesh->vertex_indices.data[ix];
                const uint32_t ni = mesh->vertex_normal.exists ? mesh->vertex_normal.indices.data[ix] : 0;
                const uint32_t ui = mesh->vertex_uv.exists ? mesh->vertex_uv.indices.data[ix] : 0;
                const VertKey key{v, ni, ui};
                auto it = map.find(key);
                uint32_t outIdx;
                if (it != map.end()) {
                    outIdx = it->second;
                } else {
                    outIdx = static_cast<uint32_t>(sm.pos.size());
                    sm.pos.push_back(V(ufbx_transform_position(&g2w, mesh->vertex_position.values.data[mesh->vertex_position.indices.data[ix]])));
                    Vec3 nn{0, 1, 0};
                    if (mesh->vertex_normal.exists) nn = V(ufbx_transform_direction(&g2w, mesh->vertex_normal.values.data[ni])).Normalized();
                    sm.nrm.push_back(nn);
                    if (mesh->vertex_uv.exists) {
                        const ufbx_vec2 uv = mesh->vertex_uv.values.data[ui];
                        sm.uv.push_back((float)uv.x);
                        sm.uv.push_back(1.f - (float)uv.y);
                    } else {
                        sm.uv.push_back(0); sm.uv.push_back(0);
                    }
                    sm.joints.push_back(vJoints[v]);
                    sm.weights.push_back(vWeights[v]);
                    map.emplace(key, outIdx);
                    back[v].push_back(outIdx);
                }
                sm.indices.push_back(outIdx);
            }
        }

        // Morph targets (blend shapes), deltas in model space
        if (opt.loadMorphs) {
            for (const ufbx_blend_deformer* bd : mesh->blend_deformers) {
                for (const ufbx_blend_channel* ch : bd->channels) {
                    const ufbx_blend_shape* shape = ch->target_shape;
                    if (!shape && ch->keyframes.count) shape = ch->keyframes.data[ch->keyframes.count - 1].shape;
                    if (!shape) continue;
                    for (auto& [slot, outIdx] : groupOf) {
                        Morph m;
                        m.name = Str(ch->name);
                        const auto& back = fbxToOut[slot];
                        for (size_t k = 0; k < shape->num_offsets; ++k) {
                            const uint32_t v = shape->offset_vertices.data[k];
                            if (v >= back.size()) continue;
                            const Vec3 d = V(ufbx_transform_direction(&g2w, shape->position_offsets.data[k]));
                            if (d.Length() < 1e-7f) continue;
                            for (uint32_t o : back[v]) { m.index.push_back(o); m.dpos.push_back(d); }
                        }
                        if (!m.index.empty()) a.meshes[outIdx].morphs.push_back(std::move(m));
                    }
                }
            }
        }
    }

    ufbx_free_scene(scene);

    if (a.meshes.empty()) {
        if (error) *error = "no meshes in " + path;
        return false;
    }

    // ---- Optional rescale to a target height ----
    if (opt.targetHeight > 0.f) {
        float lo = 1e9f, hi = -1e9f;
        for (const SkinMesh& m : a.meshes) for (const Vec3& p : m.pos) { lo = std::min(lo, p.y); hi = std::max(hi, p.y); }
        const float h = hi - lo;
        if (h > 1e-4f) {
            const float k = opt.targetHeight / h;
            for (SkinMesh& m : a.meshes) {
                for (Vec3& p : m.pos) p = p * k;
                for (Morph& mo : m.morphs) for (Vec3& d : mo.dpos) d = d * k;
            }
            for (Bone& b : a.bones) b.bindLocal.t = b.bindLocal.t * k;
            std::vector<Transform> w(a.bones.size());
            for (size_t i = 0; i < a.bones.size(); ++i) {
                const int p = a.bones[i].parent;
                w[i] = p >= 0 ? w[p] * a.bones[i].bindLocal : a.bones[i].bindLocal;
                a.bones[i].bindWorld = Mat4::FromTransform(w[i]);
                a.bones[i].invBind = Mat4::FromTransform(w[i].Inverse());
            }
        }
    }

    out = std::move(a);
    return true;
}

} // namespace eon
