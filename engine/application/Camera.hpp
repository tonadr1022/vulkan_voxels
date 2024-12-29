#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

union SDL_Event;
class Camera {
 public:
  glm::vec3 velocity{0};
  glm::vec3 position;
  float pitch{0}, yaw{0};
  float move_speed_mult{.05f};
  float look_speed_inv{400.f};
  void LookAt(const glm::vec3& pos);
  [[nodiscard]] glm::mat4 GetView() const;
  [[nodiscard]] glm::mat4 GetRotationMat() const;

  bool OnEvent(const SDL_Event& e);
  void Update();

  [[nodiscard]] glm::vec3 GetLookDirection() const;

 private:
};
