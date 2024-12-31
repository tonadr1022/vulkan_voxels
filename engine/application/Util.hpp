#pragma once

#include "application/ImageData.hpp"

namespace util {
void WriteImage(std::string_view path, const ImageData& data);
std::string GetCacheFilePath(const std::string& app_name);
std::string GetEnvVar(std::string const& key);
std::string ExecCommand(const std::string& command);

}  // namespace util
