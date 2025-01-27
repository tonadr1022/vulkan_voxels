#pragma once

#include <fmt/core.h>
#include <vulkan/vulkan.h>

#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#include <unordered_map>
#include <vector>

#if defined _WIN32 || defined __CYGWIN__
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

#define RESOURCE_DIR WORKING_DIR PATH_SEP "resources"
#define SHADER_DIR RESOURCE_DIR PATH_SEP "shaders"
#define SHADER_PATH(shader_path) \
  (std::string(WORKING_DIR) + PATH_SEP "resources" PATH_SEP + "shaders" + PATH_SEP + (shader_path))
