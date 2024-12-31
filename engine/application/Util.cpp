#include "Util.hpp"

#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
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
void WriteImage(std::string_view path, const ImageData& data) {
  if (stbi_write_png(static_cast<const char*>(path.data()), data.w, data.h, data.channels,
                     data.data, data.row_pitch) != 1) {
    fmt::println("Failed to write image: {}", path);
  }
}

}  // namespace util
