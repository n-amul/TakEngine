/*
 camera.h
 Vulkan Camera Code - Free Flying Camera
 Z-up coordinate system (Vulkan convention)
 */
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "defines.hpp"

enum CameraDirection { UP, DOWN, LEFT, RIGHT, FORWARD, BACK };

class Camera {
 public:
  Camera();
  ~Camera();

  void Reset();

  // Updates the camera matrices
  void Update();

  // Movement functions
  void Move(CameraDirection dir);
  void ChangePitch(float degrees);
  void ChangeHeading(float degrees);
  void Move2D(int x, int y);

  // Setting functions
  void SetPosition(glm::vec3 pos);
  void SetLookAt(glm::vec3 pos);
  void SetFOV(double fov);
  void SetViewport(int loc_x, int loc_y, int width, int height);
  void SetClipping(double near_clip_distance, double far_clip_distance);

  // Mouse input handling
  void SetMouseButton(int button, bool pressed, int x, int y);
  void SetScrollWheel(float delta);

  // Getting functions
  void GetViewport(int &loc_x, int &loc_y, int &width, int &height);
  void GetMatrices(glm::mat4 &P, glm::mat4 &V, glm::mat4 &M);
  glm::mat4 GetProjectionMatrix() const { return projection; }
  glm::mat4 GetViewMatrix() const { return view; }
  glm::mat4 GetModelMatrix() const { return model; }
  glm::mat4 GetMVP() const { return MVP; }
  glm::vec3 GetPosition() const { return camera_position; }
  glm::vec3 GetDirection() const { return camera_direction; }

 private:
  // Viewport
  int viewport_x;
  int viewport_y;
  int window_width;
  int window_height;
  double aspect;

  // Camera parameters
  double field_of_view;
  double near_clip;
  double far_clip;
  float camera_scale;
  float camera_heading;
  float camera_pitch;
  float max_pitch_rate;
  float max_heading_rate;
  bool move_camera;

  // Camera vectors
  glm::vec3 camera_position;
  glm::vec3 camera_position_delta;
  glm::vec3 camera_look_at;
  glm::vec3 camera_direction;
  glm::vec3 camera_up;
  glm::vec3 camera_right;
  glm::vec3 mouse_position;

  // Matrices
  glm::mat4 projection;
  glm::mat4 view;
  glm::mat4 model;
  glm::mat4 MVP;

  // Helper function to update camera basis vectors
  void UpdateCameraVectors();
};