#include "Terrain.h"

#include <algorithm>
#include <cmath>

namespace eon {

void Heightfield::Resize(int samples, float sx, float sz) {
    n = std::max(4, samples);
    sizeX = sx;
    sizeZ = sz;
    h.assign(static_cast<size_t>(n) * n, 0.f);
}

bool Heightfield::Inside(float wx, float wz) const {
    const float x = wx - origin.x, z = wz - origin.z;
    return x >= 0.f && z >= 0.f && x <= sizeX && z <= sizeZ;
}

float Heightfield::HeightAt(float wx, float wz) const {
    if (h.empty()) return origin.y;
    const float fx = std::clamp((wx - origin.x) / CellX(), 0.f, static_cast<float>(n - 1) - 1e-4f);
    const float fz = std::clamp((wz - origin.z) / CellZ(), 0.f, static_cast<float>(n - 1) - 1e-4f);
    const int x = static_cast<int>(fx), z = static_cast<int>(fz);
    const float tx = fx - x, tz = fz - z;
    const float a = At(x, z) * (1 - tx) + At(x + 1, z) * tx;
    const float b = At(x, z + 1) * (1 - tx) + At(x + 1, z + 1) * tx;
    return origin.y + a * (1 - tz) + b * tz;
}

Vec3 Heightfield::NormalAt(float wx, float wz) const {
    const float e = 0.5f * std::min(CellX(), CellZ());
    const float dx = HeightAt(wx + e, wz) - HeightAt(wx - e, wz);
    const float dz = HeightAt(wx, wz + e) - HeightAt(wx, wz - e);
    return Vec3{-dx, 2.f * e, -dz}.Normalized({0, 1, 0});
}

namespace {
float Hash(int x, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffff) / 16777215.f;
}
float ValueNoise(float x, float z, uint32_t seed) {
    const int xi = static_cast<int>(std::floor(x)), zi = static_cast<int>(std::floor(z));
    const float tx = x - xi, tz = z - zi;
    const float sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
    const float a = Hash(xi, zi, seed), b = Hash(xi + 1, zi, seed), c = Hash(xi, zi + 1, seed), d = Hash(xi + 1, zi + 1, seed);
    return (a * (1 - sx) + b * sx) * (1 - sz) + (c * (1 - sx) + d * sx) * sz;
}
float Falloff(float d, float r) { return d >= r ? 0.f : 0.5f + 0.5f * std::cos(3.14159265f * d / r); }

template <class F>
void ForBrush(Heightfield& hf, float wx, float wz, float radius, F fn) {
    const int x0 = std::max(0, static_cast<int>(std::floor((wx - radius - hf.origin.x) / hf.CellX())));
    const int x1 = std::min(hf.n - 1, static_cast<int>(std::ceil((wx + radius - hf.origin.x) / hf.CellX())));
    const int z0 = std::max(0, static_cast<int>(std::floor((wz - radius - hf.origin.z) / hf.CellZ())));
    const int z1 = std::min(hf.n - 1, static_cast<int>(std::ceil((wz + radius - hf.origin.z) / hf.CellZ())));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            const float px = hf.origin.x + x * hf.CellX(), pz = hf.origin.z + z * hf.CellZ();
            const float w = Falloff(std::sqrt((px - wx) * (px - wx) + (pz - wz) * (pz - wz)), radius);
            if (w > 0.f) fn(x, z, w);
        }
}
}

void GenerateTerrain(Heightfield& hf, const TerrainGen& g) {
    for (int z = 0; z < hf.n; ++z)
        for (int x = 0; x < hf.n; ++x) {
            const float wx = x * hf.CellX(), wz = z * hf.CellZ();
            float f = 1.f / std::max(0.5f, g.feature), amp = 1.f, sum = 0.f, norm = 0.f;
            for (int o = 0; o < g.octaves; ++o) {
                sum += ValueNoise(wx * f, wz * f, g.seed + o * 101u) * amp;
                norm += amp;
                amp *= g.roughness;
                f *= 2.f;
            }
            hf.At(x, z) = (sum / norm - 0.5f) * 2.f * g.amplitude * 0.5f;
        }
}

void BrushRaise(Heightfield& hf, float wx, float wz, float radius, float metres) {
    ForBrush(hf, wx, wz, radius, [&](int x, int z, float w) { hf.At(x, z) += metres * w; });
}

void BrushSmooth(Heightfield& hf, float wx, float wz, float radius, float strength) {
    const std::vector<float> src = hf.h;
    ForBrush(hf, wx, wz, radius, [&](int x, int z, float w) {
        float s = 0.f;
        int c = 0;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int a = x + dx, b = z + dz;
                if (a < 0 || b < 0 || a >= hf.n || b >= hf.n) continue;
                s += src[static_cast<size_t>(b) * hf.n + a];
                ++c;
            }
        hf.At(x, z) += (s / c - hf.At(x, z)) * std::clamp(strength * w, 0.f, 1.f);
    });
}

void BrushFlatten(Heightfield& hf, float wx, float wz, float radius, float worldY, float strength) {
    const float target = worldY - hf.origin.y;
    ForBrush(hf, wx, wz, radius, [&](int x, int z, float w) { hf.At(x, z) += (target - hf.At(x, z)) * std::clamp(strength * w, 0.f, 1.f); });
}

void CarveBasin(Heightfield& hf, float wx, float wz, float radius, float depth) {
    // a bowl that blends into the ground around it: lowest at the middle, never raises anything
    const float rim = hf.HeightAt(wx, wz) - hf.origin.y;
    ForBrush(hf, wx, wz, radius, [&](int x, int z, float w) {
        const float bed = rim - depth * w;
        hf.At(x, z) = std::min(hf.At(x, z), bed + (hf.At(x, z) - bed) * (1.f - w));
    });
}

namespace {
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string EncodeHeights(const Heightfield& hf) {
    float lo = 1e9f, hi = -1e9f;
    for (float v : hf.h) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (hf.h.empty()) lo = hi = 0.f;
    const float span = std::max(hi - lo, 1e-4f);
    std::vector<uint8_t> bytes;
    bytes.reserve(hf.h.size() * 2);
    for (float v : hf.h) {
        const uint16_t q = static_cast<uint16_t>(std::lround((v - lo) / span * 65535.f));
        bytes.push_back(q & 0xff);
        bytes.push_back(q >> 8);
    }
    std::string out = std::to_string(lo) + ":" + std::to_string(span) + ":";
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t a = bytes[i], b = i + 1 < bytes.size() ? bytes[i + 1] : 0, c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const uint32_t t = (a << 16) | (b << 8) | c;
        out += kB64[(t >> 18) & 63];
        out += kB64[(t >> 12) & 63];
        out += i + 1 < bytes.size() ? kB64[(t >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kB64[t & 63] : '=';
    }
    return out;
}

bool DecodeHeights(const std::string& s, Heightfield& hf) {
    const size_t a = s.find(':'), b = s.find(':', a + 1);
    if (a == std::string::npos || b == std::string::npos) return false;
    const float lo = std::stof(s.substr(0, a)), span = std::stof(s.substr(a + 1, b - a - 1));
    int rev[256];
    std::fill(rev, rev + 256, -1);
    for (int i = 0; i < 64; ++i) rev[static_cast<unsigned char>(kB64[i])] = i;
    std::vector<uint8_t> bytes;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = b + 1; i < s.size(); ++i) {
        const int v = rev[static_cast<unsigned char>(s[i])];
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; bytes.push_back(static_cast<uint8_t>((acc >> bits) & 0xff)); }
    }
    if (bytes.size() < hf.h.size() * 2) return false;
    for (size_t i = 0; i < hf.h.size(); ++i) hf.h[i] = lo + span * static_cast<float>(bytes[2 * i] | (bytes[2 * i + 1] << 8)) / 65535.f;
    return true;
}

void BuildTerrainMeshes(const Heightfield& hf, const std::vector<WaterBox>& water, int entity, uint32_t version, std::vector<RenderMesh>& out) {
    // which samples are under water, then how far (m) every sample is from the nearest one: the shore is sand
    const int n = hf.n;
    std::vector<float> wet(static_cast<size_t>(n) * n, 1e9f);
    std::vector<float> surf(static_cast<size_t>(n) * n, -1e9f);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const float wx = hf.origin.x + x * hf.CellX(), wz = hf.origin.z + z * hf.CellZ(), y = hf.origin.y + hf.At(x, z);
            for (const WaterBox& w : water)
                if (wx >= w.lo.x && wx <= w.hi.x && wz >= w.lo.z && wz <= w.hi.z && y < w.hi.y) { wet[z * n + x] = 0.f; surf[z * n + x] = w.hi.y; }
        }
    const float cx = hf.CellX(), cz = hf.CellZ(), cd = std::sqrt(cx * cx + cz * cz);
    for (int pass = 0; pass < 2; ++pass)   // two-pass chamfer distance (good enough for a shoreline)
        for (int i = 0; i < n * n; ++i) {
            const int k = pass == 0 ? i : n * n - 1 - i;
            const int x = k % n, z = k / n, d = pass == 0 ? -1 : 1;
            auto relax = [&](int ax, int az, float step) {
                if (ax < 0 || az < 0 || ax >= n || az >= n) return;
                const size_t o = static_cast<size_t>(az) * n + ax;
                if (wet[o] + step < wet[k]) { wet[k] = wet[o] + step; surf[k] = surf[o]; }
            };
            relax(x + d, z, cx); relax(x, z + d, cz); relax(x + d, z + d, cd); relax(x - d, z + d, cd);
        }
    const int tile = 64;
    for (int tz = 0; tz < hf.n - 1; tz += tile)
        for (int tx = 0; tx < hf.n - 1; tx += tile) {
            RenderMesh m;
            m.entity = entity;
            m.version = version;
            const int nx = std::min(tile, hf.n - 1 - tx) + 1, nz = std::min(tile, hf.n - 1 - tz) + 1;
            for (int z = 0; z < nz; ++z)
                for (int x = 0; x < nx; ++x) {
                    const int gx = tx + x, gz = tz + z;
                    const float wx = hf.origin.x + gx * hf.CellX(), wz = hf.origin.z + gz * hf.CellZ();
                    const float y = hf.origin.y + hf.At(gx, gz);
                    const Vec3 nrm = hf.NormalAt(wx, wz);
                    m.pos.push_back({wx, y, wz});
                    m.nrm.push_back(nrm);
                    // ground colour: rock on steep slopes, sand at the waterline, mud under water, grass elsewhere
                    const float slope = 1.f - nrm.y;
                    const size_t gi = static_cast<size_t>(gz) * n + gx;
                    const bool under = wet[gi] == 0.f;
                    const float above = wet[gi] < 1.4f ? y - surf[gi] : 1e9f;   // a shore's width from the water
                    const float jitter = (Hash(gx, gz, 7) - 0.5f) * 14.f;
                    auto c = [&](float r, float g, float b) { return Rgb{static_cast<unsigned char>(std::clamp(r + jitter, 0.f, 255.f)), static_cast<unsigned char>(std::clamp(g + jitter, 0.f, 255.f)), static_cast<unsigned char>(std::clamp(b + jitter * 0.6f, 0.f, 255.f))}; };
                    Rgb col;
                    if (slope > 0.35f) col = c(118, 112, 104);
                    else if (under) col = c(92, 78, 58);
                    else if (above < 0.3f) col = c(194, 178, 128);
                    else if (slope > 0.18f) col = c(120, 98, 66);
                    else col = c(86, 128, 58);
                    m.col.push_back(col);
                }
            for (int z = 0; z < nz - 1; ++z)
                for (int x = 0; x < nx - 1; ++x) {
                    const uint32_t a = z * nx + x, b = a + 1, c = a + nx, d = c + 1;
                    m.idx.insert(m.idx.end(), {a, c, b, b, c, d});
                }
            out.push_back(std::move(m));
        }
}

} // namespace eon
