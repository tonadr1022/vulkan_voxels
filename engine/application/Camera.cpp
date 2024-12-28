#include "Camera.hpp"

#include "SDL3/SDL_events.h"
#include "glm/ext/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"

void Camera::Update() {
  glm::mat4 rot = GetRotationMat();
  position += glm::vec3(rot * glm::vec4(glm::vec3(velocity.x, 0, velocity.z) * move_speed_mult, 0));
  position += glm::vec3(0, velocity.y * move_speed_mult, 0);
}

bool Camera::OnEvent(const SDL_Event& e) {
  bool handled = false;
  if (e.type == SDL_EVENT_KEY_DOWN) {
    auto key = e.key.key;
    if (key == SDLK_W || key == SDLK_I) {
      handled = true;
      velocity.z = -1;
    } else if (key == SDLK_S || key == SDLK_K) {
      handled = true;
      velocity.z = 1;
    } else if (key == SDLK_A || key == SDLK_J) {
      handled = true;
      velocity.x = -1;
    } else if (key == SDLK_D || key == SDLK_L) {
      handled = true;
      velocity.x = 1;
    } else if (key == SDLK_Y || key == SDLK_R) {
      handled = true;
      velocity.y = 1;
    } else if (key == SDLK_H || key == SDLK_F) {
      handled = true;
      velocity.y = -1;
    }
  } else if (e.type == SDL_EVENT_KEY_UP) {
    auto key = e.key.key;
    if (key == SDLK_W || key == SDLK_I || key == SDLK_S || key == SDLK_K) {
      handled = true;
      velocity.z = 0;
    } else if (key == SDLK_A || key == SDLK_J || key == SDLK_D || key == SDLK_L) {
      handled = true;
      velocity.x = 0;
    } else if (key == SDLK_Y || key == SDLK_R || key == SDLK_H || key == SDLK_F) {
      handled = true;
      velocity.y = 0;
    }
  } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
    handled = true;
    yaw -= static_cast<float>(e.motion.xrel / look_speed_inv);
    pitch -= static_cast<float>(e.motion.yrel / look_speed_inv);
  }
  if (handled) {
    fmt::println("update cmaera");
  }
  return handled;
}

glm::mat4 Camera::GetView() const {
  glm::mat4 cam_translation = glm::translate(glm::mat4{1}, position);
  return glm::inverse(cam_translation * GetRotationMat());
}

void Camera::LookAt(const glm::vec3& pos) {
  glm::vec3 look_dir = glm::normalize(pos - position);
  pitch = std::asin(look_dir.y);
  yaw = -std::atan2(look_dir.x, -look_dir.z);
}

glm::mat4 Camera::GetRotationMat() const {
  glm::quat pitch_rot = glm::angleAxis(pitch, glm::vec3{1, 0, 0});
  glm::quat yaw_rot = glm::angleAxis(yaw, glm::vec3{0, 1, 0});
  return glm::toMat4(yaw_rot) * glm::toMat4(pitch_rot);
}
