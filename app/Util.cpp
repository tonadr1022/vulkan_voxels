#include "Util.hpp"

#include <filesystem>

namespace util {

namespace {

std::string GetDateTimeForFilename() {
  auto now = std::chrono::system_clock::now();
  std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  std::tm now_tm = *std::localtime(&now_time_t);
  std::ostringstream oss;
  oss << std::put_time(&now_tm, "%Y-%m-%d_%H-%M-%S");
  return oss.str();
}

}  // namespace

std::string GetScreenshotPath(std::string_view filename, bool include_timestamp) {
  std::string full_path;
  std::filesystem::path screenshot_path{GET_PATH("local_screenshots")};
  if (!std::filesystem::exists(screenshot_path)) {
    std::filesystem::create_directory(screenshot_path);
  }
  if (include_timestamp) {
    full_path = screenshot_path / (std::string(filename) + "_" + GetDateTimeForFilename() + ".png");
  } else {
    full_path = screenshot_path / (std::string(filename) + ".png");
  }
  return full_path;
}

size_t Align(size_t value, size_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }
}  // namespace util
