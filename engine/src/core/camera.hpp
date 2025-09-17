#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "kMath.hpp"

class Camera {
 public:
  Camera()
      : position(2.0f, 2.0f, 3.0f),
        orientation(1.0f, 0.0f, 0.0f, 0.0f)  // Identity quaternion
        ,
        worldUp(0.0f, 1.0f, 0.0f),
        movementSpeed(1.0f),
        mouseSensitivity(0.002f),
        rollSpeed(1.0f),
        fov(45.0f * 3.14159f / 180.0f)  // Convert to radians
        ,
        nearPlane(0.1f),
        farPlane(1000.0f),
        velocity(0.0f, 0.0f, 0.0f),
        damping(0.9f) {
    updateVectors();
  }

  void initialize(const glm::vec3& pos, const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)) {
    position = Vec3(pos.x, pos.y, pos.z);
    worldUp = Vec3(up.x, up.y, up.z).normalized();

    // Calculate initial orientation from look-at
    Vec3 direction = Vec3(target.x - pos.x, target.y - pos.y, target.z - pos.z).normalized();
    Vec3 right = direction.cross(worldUp).normalized();
    Vec3 cameraUp = right.cross(direction);

    // Create quaternion from direction vectors
    // This is a simplified version - for full implementation, convert rotation matrix to quaternion
    orientation = Quat(1.0f, 0.0f, 0.0f, 0.0f);  // Start with identity
    updateVectors();

    // Apply initial rotations to face target
    Vec3 toTarget = Vec3(target.x - pos.x, target.y - pos.y, target.z - pos.z).normalized();
    float yaw = std::atan2(toTarget.x, -toTarget.z);
    float pitch = std::asin(-toTarget.y);

    Quat yawRot = Quat::fromAxisAngle(worldUp, yaw);
    Quat pitchRot = Quat::fromAxisAngle(Vec3(1, 0, 0), pitch);
    orientation = yawRot * pitchRot;

    updateVectors();
  }

  // Movement methods
  void moveForward(float delta) { velocity += forward * movementSpeed; }
  void moveBackward(float delta) { velocity -= forward * movementSpeed; }
  void moveLeft(float delta) { velocity -= right * movementSpeed; }
  void moveRight(float delta) { velocity += right * movementSpeed; }
  void moveUp(float delta) { velocity += up * movementSpeed; }
  void moveDown(float delta) { velocity -= up * movementSpeed; }

  // Rotation methods
  void rotate(float yawDelta, float pitchDelta) {
    // Yaw: around world up axis
    Quat yawRotation = Quat::fromAxisAngle(worldUp, yawDelta * mouseSensitivity);

    // Apply pitch (rotation around local right axis)
    Quat pitchRotation = Quat::fromAxisAngle(right, pitchDelta * mouseSensitivity);

    // Combine rotations
    orientation = (yawRotation * pitchRotation * orientation).normalized();
    updateVectors();
  }

  void roll(float rollDelta) {
    Quat rollRotation = Quat::fromAxisAngle(forward, rollDelta * rollSpeed);
    orientation = (rollRotation * orientation).normalized();
    updateVectors();
  }

  // Update camera (call once per frame)
  void update(float deltaTime) {
    position += velocity * deltaTime;
    velocity = Vec3(0, 0, 0);
  }

  // Get view matrix compatible with GLM
  glm::mat4 getViewMatrix() const {
    // Create view matrix from quaternion and position
    Mat4 rotation = Mat4::fromQuaternion(orientation);
    Mat4 translation = Mat4::translation(Vec3(-position.x, -position.y, -position.z));
    Mat4 view = translation * rotation;

    // Convert to GLM matrix
    glm::mat4 result;
    for (int i = 0; i < 16; i++) {
      result[i / 4][i % 4] = view.m[i];
    }
    return result;
  }

  // Get projection matrix compatible with GLM
  glm::mat4 getProjectionMatrix(float aspectRatio) const {
    Mat4 proj = Mat4::perspective(fov, aspectRatio, nearPlane, farPlane);

    // Convert to GLM matrix
    glm::mat4 result;
    for (int i = 0; i < 16; i++) {
      result[i / 4][i % 4] = proj.m[i];
    }
    return result;
  }

  // Setters
  void setPosition(const glm::vec3& pos) { position = Vec3(pos.x, pos.y, pos.z); }
  void setSpeed(float speed) { movementSpeed = speed; }
  void setSensitivity(float sens) { mouseSensitivity = sens; }
  void setFov(float fovDegrees) { fov = fovDegrees * 3.14159f / 180.0f; }
  void setNearPlane(float near) { nearPlane = near; }
  void setFarPlane(float far) { farPlane = far; }

  // Getters
  glm::vec3 getPosition() const { return glm::vec3(position.x, position.y, position.z); }
  glm::vec3 getForward() const { return glm::vec3(forward.x, forward.y, forward.z); }
  glm::vec3 getRight() const { return glm::vec3(right.x, right.y, right.z); }
  glm::vec3 getUp() const { return glm::vec3(up.x, up.y, up.z); }
  float getFov() const { return fov * 180.0f / 3.14159f; }  // Return in degrees

 private:
  // Camera properties
  Vec3 position;
  Quat orientation;

  // Direction vectors
  Vec3 forward;
  Vec3 right;
  Vec3 up;
  Vec3 worldUp;

  // Movement parameters
  float movementSpeed;
  float mouseSensitivity;
  float rollSpeed;

  // Projection parameters
  float fov;
  float nearPlane;
  float farPlane;

  // Movement physics
  Vec3 velocity;
  float damping;

  void updateVectors() {
    // Default basis vectors
    Vec3 defaultForward(0, 0, -1);
    Vec3 defaultRight(1, 0, 0);
    Vec3 defaultUp(0, 1, 0);

    // Rotate basis vectors by quaternion
    forward = orientation.rotate(defaultForward).normalized();
    right = orientation.rotate(defaultRight).normalized();
    up = orientation.rotate(defaultUp).normalized();
  }
};