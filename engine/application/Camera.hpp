#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

union SDL_Event;
class Camera {
 public:
  static constexpr glm::vec3 WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
  static constexpr float DefaultMouseSensitivity = 0.075f;
  glm::vec3 position{};
  glm::vec3 front{0, 0, -1};
  glm::vec3 up{0, 1, 0};
  glm::vec3 right;
  float pitch{0}, yaw{0};
  float sensitivity{DefaultMouseSensitivity};
  float move_speed_mult{.05f};
  float look_speed_inv{400.f};
  void LookAt(const glm::vec3& pos);
  void UpdatePosition(glm::vec3 pos) { position = pos; }
  void UpdateRotation(float mouse_x_off, float mouse_y_off);
  [[nodiscard]] glm::mat4 GetView() const;

 private:
  void UpdateVectors();
};
