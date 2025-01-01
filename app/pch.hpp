#pragma once

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <vector>

#include "EAssert.hpp"

#if defined _WIN32 || defined __CYGWIN__
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

#define GET_PATH(x) WORKING_DIR PATH_SEP x

using vec3 = glm::vec3;
using vec2 = glm::vec2;
using ivec2 = glm::ivec2;
using ivec3 = glm::ivec3;
using mat4 = glm::mat4;
using uvec3 = glm::uvec3;
using uvec2 = glm::uvec2;
using vec4 = glm::vec4;
using i8vec3 = glm::i8vec3;
using uvec4 = glm::uvec4;
using ivec4 = glm::ivec4;

template <typename T, size_t Size>
char (*countof_helper(T (&_array_)[Size]))[Size];

#define COUNTOF(array) (sizeof(*countof_helper(array)) + 0)
