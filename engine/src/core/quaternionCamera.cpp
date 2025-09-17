#include "QuaternionCamera.hpp"

#include <algorithm>

QuaternionCamera::QuaternionCamera()
    : position(3.0f, 3.0f, 3.0f)  // Adjusted for Z-up
      ,
      orientation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f))  // Identity quaternion
      ,
      worldUp(0.0f, 0.0f, 1.0f)  // Z-up world
      ,
      movementSpeed(5.0f),
      mouseSensitivity(0.002f),
      rollSpeed(1.0f),
      fov(glm::radians(45.0f)),
      nearPlane(0.1f),
      farPlane(1000.0f),
      velocity(0.0f),
      damping(0.8f) {
  updateVectors();
}

void QuaternionCamera::initialize(const glm::vec3& pos, const glm::vec3& target, const glm::vec3& up) {
  position = pos;
  worldUp = glm::normalize(up);

  // Calculate initial orientation quaternion from look-at
  glm::vec3 direction = glm::normalize(target - pos);

  // For Z-up coordinate system
  glm::vec3 right = glm::normalize(glm::cross(direction, worldUp));
  glm::vec3 cameraUp = glm::cross(right, direction);

  glm::mat3 rotationMatrix;
  rotationMatrix[0] = right;
  rotationMatrix[1] = cameraUp;
  rotationMatrix[2] = -direction;  // Camera looks down -Z in view space

  orientation = glm::quat_cast(rotationMatrix);
  updateVectors();
}

void QuaternionCamera::moveForward() { velocity += forward * movementSpeed; }

void QuaternionCamera::moveBackward() { velocity -= forward * movementSpeed; }

void QuaternionCamera::moveLeft() { velocity -= right * movementSpeed; }

void QuaternionCamera::moveRight() { velocity += right * movementSpeed; }

void QuaternionCamera::moveUp() { velocity += up * movementSpeed; }

void QuaternionCamera::moveDown() { velocity -= up * movementSpeed; }

void QuaternionCamera::rotate(float yawDelta, float pitchDelta) {
  // Apply yaw (rotation around world up axis)
  glm::quat yawRotation = glm::angleAxis(-yawDelta * mouseSensitivity, worldUp);

  // Apply pitch (rotation around local right axis)
  glm::quat pitchRotation = glm::angleAxis(-pitchDelta * mouseSensitivity, right);

  // Combine rotations: first pitch (local), then yaw (world)
  orientation = yawRotation * pitchRotation * orientation;
  orientation = glm::normalize(orientation);

  updateVectors();
}

void QuaternionCamera::roll(float rollDelta) {
  // Roll around forward axis
  glm::quat rollRotation = glm::angleAxis(rollDelta * rollSpeed, forward);
  orientation = rollRotation * orientation;
  orientation = glm::normalize(orientation);

  updateVectors();
}

void QuaternionCamera::update(float deltaTime) {
  // Apply velocity with damping for smooth movement
  position += velocity * deltaTime;
  velocity *= damping;  // Gradual slowdown

  // Stop very small movements
  if (glm::length(velocity) < 0.01f) {
    velocity = glm::vec3(0.0f);
  }
}

glm::mat4 QuaternionCamera::getViewMatrix() const {
  // Create view matrix from position and orientation
  glm::mat4 rotation = glm::mat4_cast(glm::conjugate(orientation));
  glm::mat4 translation = glm::translate(glm::mat4(1.0f), -position);

  return rotation * translation;
}

glm::mat4 QuaternionCamera::getProjectionMatrix(float aspectRatio) const {
  glm::mat4 proj = glm::perspective(fov, aspectRatio, nearPlane, farPlane);
  proj[1][1] *= -1;  // Flip Y for Vulkan
  return proj;
}

void QuaternionCamera::updateVectors() {
  // Extract direction vectors from quaternion
  glm::mat3 rotationMatrix = glm::mat3_cast(orientation);

  right = rotationMatrix[0];
  up = rotationMatrix[1];
  forward = -rotationMatrix[2];  // Negative because camera convention

  // Normalize to ensure unit vectors
  right = glm::normalize(right);
  up = glm::normalize(up);
  forward = glm::normalize(forward);
}