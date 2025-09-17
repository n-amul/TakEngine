#pragma once

#include <algorithm>
#include <cmath>

// Simple math structures for camera
struct Vec3 {
  float x, y, z;

  Vec3() : x(0), y(0), z(0) {}
  Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

  Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
  Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
  Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
  Vec3& operator+=(const Vec3& v) {
    x += v.x;
    y += v.y;
    z += v.z;
    return *this;
  }
  Vec3& operator-=(const Vec3& v) {
    x -= v.x;
    y -= v.y;
    z -= v.z;
    return *this;
  }
  Vec3& operator*=(float s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }

  float dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }

  Vec3 cross(const Vec3& v) const { return Vec3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); }

  float length() const { return std::sqrt(x * x + y * y + z * z); }

  Vec3 normalized() const {
    float len = length();
    if (len > 0.0001f) {
      return Vec3(x / len, y / len, z / len);
    }
    return *this;
  }
};

struct Quat {
  float w, x, y, z;

  Quat() : w(1), x(0), y(0), z(0) {}
  Quat(float w, float x, float y, float z) : w(w), x(x), y(y), z(z) {}

  static Quat fromAxisAngle(const Vec3& axis, float angle) {
    float halfAngle = angle * 0.5f;
    float s = std::sin(halfAngle);
    Vec3 normalizedAxis = axis.normalized();
    return Quat(std::cos(halfAngle), normalizedAxis.x * s, normalizedAxis.y * s, normalizedAxis.z * s);
  }

  Quat operator*(const Quat& q) const {
    return Quat(w * q.w - x * q.x - y * q.y - z * q.z, w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x, w * q.z + x * q.y - y * q.x + z * q.w);
  }

  Quat conjugate() const { return Quat(w, -x, -y, -z); }

  Quat normalized() const {
    float len = std::sqrt(w * w + x * x + y * y + z * z);
    if (len > 0.0001f) {
      return Quat(w / len, x / len, y / len, z / len);
    }
    return *this;
  }

  Vec3 rotate(const Vec3& v) const {
    // Quaternion rotation formula: q * v * q^-1
    Quat vQuat(0, v.x, v.y, v.z);
    Quat result = (*this) * vQuat * conjugate();
    return Vec3(result.x, result.y, result.z);
  }
};

struct Mat4 {
  float m[16];  // Column-major order for OpenGL/Vulkan

  Mat4() {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1.0f;  // Identity
  }

  static Mat4 perspective(float fov, float aspect, float near, float far) {
    Mat4 result;
    float tanHalfFov = std::tan(fov * 0.5f);

    result.m[0] = 1.0f / (aspect * tanHalfFov);
    result.m[5] = 1.0f / tanHalfFov;
    result.m[10] = -(far + near) / (far - near);
    result.m[11] = -1.0f;
    result.m[14] = -(2.0f * far * near) / (far - near);
    result.m[15] = 0.0f;

    return result;
  }

  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Vec3 f = (target - eye).normalized();
    Vec3 s = f.cross(up).normalized();
    Vec3 u = s.cross(f);

    Mat4 result;
    result.m[0] = s.x;
    result.m[4] = s.y;
    result.m[8] = s.z;

    result.m[1] = u.x;
    result.m[5] = u.y;
    result.m[9] = u.z;

    result.m[2] = -f.x;
    result.m[6] = -f.y;
    result.m[10] = -f.z;

    result.m[12] = -s.dot(eye);
    result.m[13] = -u.dot(eye);
    result.m[14] = f.dot(eye);

    return result;
  }

  static Mat4 fromQuaternion(const Quat& q) {
    Mat4 result;

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);

    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);

    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);

    return result;
  }

  static Mat4 translation(const Vec3& v) {
    Mat4 result;
    result.m[12] = v.x;
    result.m[13] = v.y;
    result.m[14] = v.z;
    return result;
  }

  Mat4 operator*(const Mat4& other) const {
    Mat4 result;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        result.m[i * 4 + j] = 0;
        for (int k = 0; k < 4; k++) {
          result.m[i * 4 + j] += m[i * 4 + k] * other.m[k * 4 + j];
        }
      }
    }
    return result;
  }
};