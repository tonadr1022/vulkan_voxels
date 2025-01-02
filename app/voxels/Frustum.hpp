#pragma once
using FrustumData = vec4[6];
struct Frustum {
  Frustum() = default;
  explicit Frustum(const glm::mat4& clip_matrix);
  void SetData(const glm::mat4& clip_matrix);

  enum Plane : uint8_t { Right, Left, Top, Bottom, Front, Back };
  enum PlaneComponent : uint8_t { X, Y, Z, Dist };

  [[nodiscard]] glm::vec4 GetPlane(Plane plane) const;

  FrustumData data;

 private:
  // float data_[6][4];
};
