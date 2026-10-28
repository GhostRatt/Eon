#pragma once
// Outdoor ground: a square heightfield (n x n samples) with generation, sculpt brushes, compact save format,
// and a vertex-coloured mesh (grass / dirt / sand / rock by height and slope).
#include <cstdint>
#include <string>
#include <vector>

#include "Math.h"

namespace eon {

struct Heightfield {
    Vec3 origin;          // world position of sample (0, 0); heights are added to origin.y
    float sizeX = 20.f, sizeZ = 20.f;
    int n = 128;          // samples per side
    std::vector<float> h; // n*n, row-major by z then x

    void Resize(int samples, float sx, float sz);
    float CellX() const { return sizeX / static_cast<float>(n - 1); }
    float CellZ() const { return sizeZ / static_cast<float>(n - 1); }
    float& At(int x, int z) { return h[static_cast<size_t>(z) * n + x]; }
    float At(int x, int z) const { return h[static_cast<size_t>(z) * n + x]; }
    bool Inside(float wx, float wz) const;
    float HeightAt(float wx, float wz) const;   // world y (bilinear; clamped at the edges)
    Vec3 NormalAt(float wx, float wz) const;
};

struct TerrainGen {
    uint32_t seed = 1;
    float amplitude = 2.f;   // m, peak-to-valley-ish
    float feature = 12.f;    // m, size of the biggest hills
    int octaves = 5;
    float roughness = 0.5f;  // amplitude kept per octave
};
void GenerateTerrain(Heightfield& hf, const TerrainGen& g);

// Sculpt brushes (world coords, radius in m, smooth cosine falloff)
void BrushRaise(Heightfield& hf, float wx, float wz, float radius, float metres);   // negative lowers
void BrushSmooth(Heightfield& hf, float wx, float wz, float radius, float strength);  // 0..1
void BrushFlatten(Heightfield& hf, float wx, float wz, float radius, float worldY, float strength);
void CarveBasin(Heightfield& hf, float wx, float wz, float radius, float depth);    // a pond/lake bed

// Compact text form for zone files: 16-bit quantized heights, base64.
std::string EncodeHeights(const Heightfield& hf);
bool DecodeHeights(const std::string& s, Heightfield& hf);   // hf.n must already match

// Mesh for drawing (tiles of at most 64x64 quads so each fits 16-bit indices)
struct RenderMesh {
    std::vector<Vec3> pos, nrm;
    std::vector<Rgb> col;
    std::vector<uint32_t> idx;
    int entity = 0;
    uint32_t version = 0;   // bump when it changes: the renderer re-uploads
    float alpha = 1.f;
};
struct WaterBox { Vec3 lo, hi; };   // a body of water: its top is the surface
void BuildTerrainMeshes(const Heightfield& hf, const std::vector<WaterBox>& water, int entity, uint32_t version, std::vector<RenderMesh>& out);

} // namespace eon
