#include "Frustum.hpp"

#include <glm/ext/matrix_float4x4.hpp>

Frustum::Frustum(const glm::mat4& clip_matrix) { SetData(clip_matrix); }

void Frustum::SetData(const glm::mat4& clip_matrix) {
  data[Left][X] = clip_matrix[0][3] + clip_matrix[0][0];
  data[Left][Y] = clip_matrix[1][3] + clip_matrix[1][0];
  data[Left][Z] = clip_matrix[2][3] + clip_matrix[2][0];
  data[Left][Dist] = clip_matrix[3][3] + clip_matrix[3][0];

  data[Right][X] = clip_matrix[0][3] - clip_matrix[0][0];
  data[Right][Y] = clip_matrix[1][3] - clip_matrix[1][0];
  data[Right][Z] = clip_matrix[2][3] - clip_matrix[2][0];
  data[Right][Dist] = clip_matrix[3][3] - clip_matrix[3][0];

  data[Top][X] = clip_matrix[0][3] - clip_matrix[0][1];
  data[Top][Y] = clip_matrix[1][3] - clip_matrix[1][1];
  data[Top][Z] = clip_matrix[2][3] - clip_matrix[2][1];
  data[Top][Dist] = clip_matrix[3][3] - clip_matrix[3][1];

  data[Bottom][X] = clip_matrix[0][3] + clip_matrix[0][1];
  data[Bottom][Y] = clip_matrix[1][3] + clip_matrix[1][1];
  data[Bottom][Z] = clip_matrix[2][3] + clip_matrix[2][1];
  data[Bottom][Dist] = clip_matrix[3][3] + clip_matrix[3][1];

  data[Front][X] = clip_matrix[0][3] - clip_matrix[0][2];
  data[Front][Y] = clip_matrix[1][3] - clip_matrix[1][2];
  data[Front][Z] = clip_matrix[2][3] - clip_matrix[2][2];
  data[Front][Dist] = clip_matrix[3][3] - clip_matrix[3][2];

  data[Back][X] = clip_matrix[0][3] + clip_matrix[0][2];
  data[Back][Y] = clip_matrix[1][3] + clip_matrix[1][2];
  data[Back][Z] = clip_matrix[2][3] + clip_matrix[2][2];
  data[Back][Dist] = clip_matrix[3][3] + clip_matrix[3][2];

  // Normalize
  for (auto& plane : data) {
    float length = glm::sqrt((plane[X] * plane[X]) + (plane[Y] * plane[Y]) + (plane[Z] * plane[Z]));
    plane[X] /= length;
    plane[Y] /= length;
    plane[Z] /= length;
    plane[Dist] /= length;
  }
}
