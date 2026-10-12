#pragma once
// 3x4 rigid transforms (same layout as vr::HmdMatrix34_t).
#include <cmath>

struct Mat34 {
    float m[3][4];

    static Mat34 identity() {
        Mat34 r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = 1.0f;
        return r;
    }
    static Mat34 translation(float x, float y, float z) {
        Mat34 r = identity();
        r.m[0][3] = x; r.m[1][3] = y; r.m[2][3] = z;
        return r;
    }
    static Mat34 rotationX(float rad) {
        Mat34 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[1][1] = c; r.m[1][2] = -s;
        r.m[2][1] = s; r.m[2][2] = c;
        return r;
    }
    static Mat34 rotationY(float rad) {
        Mat34 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c; r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }
    static Mat34 rotationZ(float rad) {
        Mat34 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        return r;
    }
};

// Rotation part as R = Ry(yaw) * Rx(pitch) * Rz(roll), in radians.
struct Euler { float yaw, pitch, roll; };
inline Euler toEuler(const Mat34& m) {
    float sp = -m.m[1][2];
    sp = sp > 1 ? 1 : sp < -1 ? -1 : sp;
    return {std::atan2(m.m[0][2], m.m[2][2]), std::asin(sp), std::atan2(m.m[1][0], m.m[1][1])};
}

inline Mat34 operator*(const Mat34& a, const Mat34& b) {
    Mat34 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 4; ++j) {
            float v = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
            if (j == 3) v += a.m[i][3];
            r.m[i][j] = v;
        }
    return r;
}

inline Mat34 inverseRigid(const Mat34& a) {
    Mat34 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
    for (int i = 0; i < 3; ++i)
        r.m[i][3] = -(r.m[i][0] * a.m[0][3] + r.m[i][1] * a.m[1][3] + r.m[i][2] * a.m[2][3]);
    return r;
}

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float k) const { return {x * k, y * k, z * k}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const { float l = length(); return l > 1e-6f ? *this * (1.0f / l) : Vec3{0, 0, -1}; }
};

constexpr float kPi = 3.14159265f;
inline float deg(float rad) { return rad * 180.0f / kPi; }
inline float rad(float deg) { return deg * kPi / 180.0f; }

// Wraps an angle difference to [-pi, pi].
inline float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// Direction for yaw (around +Y, 0 = -Z forward) and pitch (up positive),
// in SteamVR's convention (Y up, -Z forward).
inline Vec3 fromYawPitch(float yaw, float pitch) {
    float cp = std::cos(pitch);
    return {-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}
inline void toYawPitch(const Vec3& d, float& yaw, float& pitch) {
    float h = std::sqrt(d.x * d.x + d.z * d.z);
    yaw = std::atan2(-d.x, -d.z);
    pitch = std::atan2(d.y, h);
}

// Critically damped spring towards `target` (Unity's SmoothDamp): eases in
// and out, never overshoots. `smoothTime` ~ time to get there.
inline float smoothDamp(float current, float target, float& velocity, float smoothTime, float dt) {
    smoothTime = smoothTime < 1e-4f ? 1e-4f : smoothTime;
    float omega = 2.0f / smoothTime;
    float x = omega * dt;
    float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * e;
    float out = target + (change + temp) * e;
    if ((target - current > 0.0f) == (out > target)) { out = target; velocity = 0; }
    return out;
}

// Easing curves on [0, 1].
inline float easeOutCubic(float t) { t = 1 - t; return 1 - t * t * t; }
inline float easeInCubic(float t) { return t * t * t; }
inline float easeInOutCubic(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }
