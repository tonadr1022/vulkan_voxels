#include "Util.hpp"

#include <filesystem>
namespace util {

std::string GetCacheFilePath(const std::string& app_name) {
  std::filesystem::path cache_path;
  cache_path = WORKING_DIR ".cache" PATH_SEP + app_name;
  std::filesystem::create_directories(cache_path);
  return cache_path;
}

std::string GetEnvVar(std::string const& key) {
  char* val = getenv(key.c_str());
  return val == nullptr ? std::string("") : std::string(val);
}
std::string ExecCommand(const std::string& command) {
  std::array<char, 128> buffer;
  std::string result;

  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}
}  // namespace util
