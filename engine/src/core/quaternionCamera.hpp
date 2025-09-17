#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

class QuaternionCamera {
 public:
  QuaternionCamera();

  // Initialize camera with position and look-at point
  void initialize(const glm::vec3& position, const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f));

  // Movement methods
  void moveForward();
  void moveBackward();
  void moveLeft();
  void moveRight();
  void moveUp();
  void moveDown();

  // Rotation methods - use mouse delta or key input
  void rotate(float yawDelta, float pitchDelta);
  void roll(float rollDelta);

  // Update camera (call once per frame)
  void update(float deltaTime);

  // Get matrices
  glm::mat4 getViewMatrix() const;
  glm::mat4 getProjectionMatrix(float aspectRatio) const;

  // Setters
  void setPosition(const glm::vec3& pos) { position = pos; }
  void setSpeed(float speed) { movementSpeed = speed; }
  void setSensitivity(float sens) { mouseSensitivity = sens; }
  void setFov(float fov) { this->fov = glm::radians(fov); }
  void setNearPlane(float near) { nearPlane = near; }
  void setFarPlane(float far) { farPlane = far; }

  // Getters
  glm::vec3 getPosition() const { return position; }
  glm::vec3 getForward() const { return forward; }
  glm::vec3 getRight() const { return right; }
  glm::vec3 getUp() const { return up; }
  glm::quat getOrientation() const { return orientation; }
  float getFov() const { return glm::degrees(fov); }

 private:
  // Camera properties
  glm::vec3 position;
  glm::quat orientation;  // Quaternion for rotation

  // Direction vectors (cached for performance)
  glm::vec3 forward;
  glm::vec3 right;
  glm::vec3 up;
  glm::vec3 worldUp;

  // Movement parameters
  float movementSpeed;
  float mouseSensitivity;
  float rollSpeed;

  // Projection parameters
  float fov;
  float nearPlane;
  float farPlane;

  // Accumulated movement for smooth motion
  glm::vec3 velocity;
  float damping;

  // Update cached direction vectors from quaternion
  void updateVectors();
};