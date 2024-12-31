#include "Camera.hpp"

#include "glm/ext/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"

glm::mat4 Camera::GetView() const { return glm::lookAt(position, position + front, up); }

void Camera::LookAt(const glm::vec3& pos) {
  glm::vec3 look_dir = glm::normalize(pos - position);
  pitch = glm::degrees(std::asin(glm::clamp(look_dir.y, -1.0f, 1.0f)));
  yaw = glm::degrees(std::atan2(look_dir.z, look_dir.x));
  UpdateVectors();
}

void Camera::UpdateVectors() {
  glm::vec3 f;
  f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
  f.y = sin(glm::radians(pitch));
  f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
  front = glm::normalize(f);
  right = glm::normalize(glm::cross(front, WorldUp));
  up = glm::normalize(glm::cross(right, front));
}

void Camera::UpdateRotation(float mouse_x_off, float mouse_y_off) {
  yaw += mouse_x_off * sensitivity;
  pitch += mouse_y_off * sensitivity;
  pitch = glm::clamp(pitch, -89.9f, 89.9f);
  UpdateVectors();
}
