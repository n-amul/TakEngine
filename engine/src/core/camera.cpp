#include "core/camera.hpp"

#include <algorithm>

// Force explicit instantiation to ensure consistent types
template class glm::vec<3, float, glm::packed_highp>;
template class glm::mat<4, 4, float, glm::packed_highp>;
Camera::Camera() {
  // vulkan Z-up coordinate system
  camera_up = glm::vec3(0, 0, 1);
  camera_position = glm::vec3(0, -5, 2);  // Start back and up a bit
  camera_look_at = glm::vec3(0, 0, 0);

  field_of_view = glm::radians(45.0);
  near_clip = 0.1;
  far_clip = 1000.0;

  camera_position_delta = glm::vec3(0, 0, 0);
  camera_scale = 0.0005f;
  max_pitch_rate = 2.0f;
  max_heading_rate = 1.0f;
  move_camera = false;

  camera_heading = 0.0f;
  camera_pitch = 0.0f;

  viewport_x = 0;
  viewport_y = 0;
  window_width = 800;
  window_height = 600;
  aspect = 800.0 / 600.0;

  UpdateCameraVectors();
}

Camera::~Camera() {}

void Camera::Reset() {
  camera_up = glm::vec3(0, 0, 1);  // Z-up
  camera_heading = 0.0f;
  camera_pitch = 0.0f;
  camera_position_delta = glm::vec3(0, 0, 0);
  UpdateCameraVectors();
}
// TODO: consider Cache these values and change only pitch and heading is changes
void Camera::UpdateCameraVectors() {
  // Calculate the front vector from heading and pitch
  // In Z-up system: heading rotates around Z, pitch tilts up/down
  float heading_rad = glm::radians(camera_heading);
  float pitch_rad = glm::radians(camera_pitch);
  float cos_pitch = cos(pitch_rad);
  camera_direction = glm::vec3(cos(heading_rad) * cos_pitch, sin(heading_rad) * cos_pitch, sin(pitch_rad));  // no need to normalize

  // Right vector is perpendicular to direction and world up
  camera_right = glm::cross(camera_direction, glm::vec3(0, 0, 1));
  camera_up = glm::cross(camera_right, camera_direction);
}

void Camera::Update() {
  // Update camera vectors based on current heading and pitch
  UpdateCameraVectors();

  camera_position += camera_position_delta;
  camera_position_delta *= 0.8f;  // Damping factor
  // Update look-at point
  camera_look_at = camera_position + camera_direction;
  view = glm::lookAt(camera_position, camera_look_at, glm::vec3(0, 0, 1));
  projection = glm::perspective(field_of_view, aspect, near_clip, far_clip);
  projection[1][1] *= -1;  // Flip Y for Vulkan
}

void Camera::Move(CameraDirection dir) {
  switch (dir) {
    case UP:
      // Move up in world space (positive Z)
      camera_position_delta += glm::vec3(0, 0, 1) * camera_scale;
      break;
    case DOWN:
      // Move down in world space (negative Z)
      camera_position_delta -= glm::vec3(0, 0, 1) * camera_scale;
      break;
    case LEFT:
      // Move left relative to camera
      camera_position_delta -= camera_right * camera_scale;
      break;
    case RIGHT:
      // Move right relative to camera
      camera_position_delta += camera_right * camera_scale;
      break;
    case FORWARD:
      // Move forward along camera direction
      camera_position_delta += camera_direction * camera_scale;
      break;
    case BACK:
      // Move backward along camera direction
      camera_position_delta -= camera_direction * camera_scale;
      break;
  }
}

void Camera::ChangePitch(float degrees) {
  // Clamp input rate
  degrees = std::clamp(degrees, -max_pitch_rate, max_pitch_rate);

  camera_pitch += degrees;

  // Clamp pitch to avoid gimbal lock
  camera_pitch = std::clamp(camera_pitch, -89.0f, 89.0f);
}

void Camera::ChangeHeading(float degrees) {
  // Clamp input rate
  degrees = std::clamp(degrees, -max_heading_rate, max_heading_rate);

  camera_heading += degrees;

  // Wrap heading around 360 degrees
  if (camera_heading > 360.0f) {
    camera_heading -= 360.0f;
  } else if (camera_heading < 0.0f) {
    camera_heading += 360.0f;
  }
}

void Camera::Move2D(int x, int y) {
  // Calculate mouse delta
  glm::vec3 mouse_delta = mouse_position - glm::vec3(x, y, 0);

  // If camera is being moved (mouse dragged)
  if (move_camera) {
    ChangeHeading(-0.1f * mouse_delta.x);  // Negative for intuitive control
    ChangePitch(0.1f * mouse_delta.y);
  }

  mouse_position = glm::vec3(x, y, 0);
}

void Camera::SetMouseButton(int button, bool pressed, int x, int y) {
  if (button == 0) {  // Left mouse button
    move_camera = pressed;
  } else if (button == 1 && pressed) {  // Right mouse button
    // Reset camera position delta for immediate stop
    camera_position_delta = glm::vec3(0, 0, 0);
  }
  mouse_position = glm::vec3(x, y, 0);
}

void Camera::SetScrollWheel(float delta) {
  // Use scroll wheel for moving up/down in world space
  camera_position_delta += glm::vec3(0, 0, delta * 0.1f);
}

// Setting Functions
void Camera::SetPosition(glm::vec3 pos) { camera_position = pos; }

void Camera::SetLookAt(glm::vec3 pos) {
  camera_look_at = pos;
  // Recalculate direction
  camera_direction = glm::normalize(camera_look_at - camera_position);

  // Calculate heading and pitch from direction
  camera_heading = glm::degrees(atan2(camera_direction.y, camera_direction.x));
  camera_pitch = glm::degrees(asin(camera_direction.z));
}

void Camera::SetFOV(double fov) { field_of_view = glm::radians(fov); }

void Camera::SetViewport(int loc_x, int loc_y, int width, int height) {
  viewport_x = loc_x;
  viewport_y = loc_y;
  window_width = width;
  window_height = height;
  aspect = static_cast<double>(width) / static_cast<double>(height);
}

void Camera::SetClipping(double near_clip_distance, double far_clip_distance) {
  near_clip = near_clip_distance;
  far_clip = far_clip_distance;
}

// Getting Functions
void Camera::GetViewport(int &loc_x, int &loc_y, int &width, int &height) {
  loc_x = viewport_x;
  loc_y = viewport_y;
  width = window_width;
  height = window_height;
}

void Camera::GetMatrices(glm::mat4 &P, glm::mat4 &V) {
  P = projection;
  V = view;
}