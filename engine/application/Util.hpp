#pragma once

namespace util {

std::string GetCacheFilePath(const std::string& app_name);
std::string GetEnvVar(std::string const& key);
std::string ExecCommand(const std::string& command);

}  // namespace util
