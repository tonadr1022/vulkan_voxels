#pragma once

#include <fmt/core.h>

#include <string>
#include <tracy/Tracy.hpp>
#include <unordered_map>
#include <vector>

#if defined _WIN32 || defined __CYGWIN__
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

#define GET_PATH(x) WORKING_DIR PATH_SEP x
