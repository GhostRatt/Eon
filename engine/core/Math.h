#pragma once

#include <cmath>

namespace eon {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    constexpr Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }

    float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 Cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float Length() const { return std::sqrt(Dot(*this)); }
    Vec3 Normalized(Vec3 fallback = {0, 1, 0}) const {
        const float l = Length();
        return l > 1e-6f ? *this * (1.f / l) : fallback;
    }
};

inline float Distance(const Vec3& a, const Vec3& b) { return (a - b).Length(); }

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    static Quat AxisAngle(const Vec3& axis, float rad) {
        const Vec3 a = axis.Normalized();
        const float s = std::sin(rad * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(rad * 0.5f)};
    }

    // Yaw (Y) * Pitch (X) * Roll (Z), degrees
    static Quat FromEulerDeg(const Vec3& d) {
        constexpr float k = 3.14159265358979f / 180.f;
        return AxisAngle({0, 1, 0}, d.y * k) * AxisAngle({1, 0, 0}, d.x * k) * AxisAngle({0, 0, 1}, d.z * k);
    }

    // Rotation whose local X/Y/Z axes map to the given orthonormal world axes.
    static Quat FromBasis(const Vec3& X, const Vec3& Y, const Vec3& Z) {
        const float tr = X.x + Y.y + Z.z;
        Quat q;
        if (tr > 0) {
            const float s = std::sqrt(tr + 1.f) * 2.f;
            q.w = 0.25f * s; q.x = (Y.z - Z.y) / s; q.y = (Z.x - X.z) / s; q.z = (X.y - Y.x) / s;
        } else if (X.x > Y.y && X.x > Z.z) {
            const float s = std::sqrt(1.f + X.x - Y.y - Z.z) * 2.f;
            q.w = (Y.z - Z.y) / s; q.x = 0.25f * s; q.y = (Y.x + X.y) / s; q.z = (Z.x + X.z) / s;
        } else if (Y.y > Z.z) {
            const float s = std::sqrt(1.f + Y.y - X.x - Z.z) * 2.f;
            q.w = (Z.x - X.z) / s; q.x = (Y.x + X.y) / s; q.y = 0.25f * s; q.z = (Z.y + Y.z) / s;
        } else {
            const float s = std::sqrt(1.f + Z.z - X.x - Y.y) * 2.f;
            q.w = (X.y - Y.x) / s; q.x = (Z.x + X.z) / s; q.y = (Z.y + Y.z) / s; q.z = 0.25f * s;
        }
        return q.Normalized();
    }

    // Shortest rotation taking direction a onto direction b.
    static Quat FromTo(const Vec3& from, const Vec3& to) {
        const Vec3 a = from.Normalized(), b = to.Normalized();
        const float d = a.Dot(b);
        if (d < -0.9999f) {
            Vec3 axis = Vec3{1, 0, 0}.Cross(a);
            if (axis.Length() < 1e-4f) axis = Vec3{0, 0, 1}.Cross(a);
            return AxisAngle(axis, 3.14159265358979f);
        }
        const Vec3 c = a.Cross(b);
        return Quat{c.x, c.y, c.z, 1.f + d}.Normalized();
    }

    Quat operator*(const Quat& b) const {
        return {w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }
    Quat Conjugate() const { return {-x, -y, -z, w}; }
    Quat Normalized() const {
        const float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-9f ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    Vec3 Rotate(const Vec3& v) const {
        const Vec3 u{x, y, z};
        const Vec3 t = u.Cross(v) * 2.f;
        return v + t * w + u.Cross(t);
    }
    // Axis-angle decomposition (angle in radians)
    void ToAxisAngle(Vec3& axis, float& angle) const {
        Quat q = w < 0 ? Quat{-x, -y, -z, -w} : *this;
        angle = 2.f * std::acos(std::fmin(1.f, q.w));
        const float s = std::sqrt(std::fmax(0.f, 1.f - q.w * q.w));
        axis = s < 1e-5f ? Vec3{0, 1, 0} : Vec3{q.x / s, q.y / s, q.z / s};
    }
};

} // namespace eon
