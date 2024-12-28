#include "Util.hpp"

#include <filesystem>

namespace tvk::util {

namespace {

bool IsShaderInclude(const std::filesystem::path& path) {
  std::string filename = path.filename().string();
  size_t h_pos = filename.find(".h.");
  size_t glsl_pos = filename.find(".glsl");
  return h_pos != std::string::npos && glsl_pos != std::string::npos && h_pos < glsl_pos;
}

}  // namespace

void CompileToSPIRV(const std::string& glslangValidator, const std::string& glsl_path,
                    const std::string& output_path) {
  std::string command = glslangValidator + " -V " + glsl_path + " -o " + output_path;
#ifndef NDEBUG
  command += " -gVS";
#endif
  int result = std::system(command.c_str());
  if (result != 0) {
    fmt::println("Error compiling: {}", glsl_path);
  }
}
bool IsShader(const std::string& path) {
  std::filesystem::path fpath{path};
  return fpath.has_extension() && !IsShaderInclude(fpath) &&
         (fpath.extension() == ".glsl" || fpath.extension() == ".hlsl");
}
std::string FindGlslangValidator() {
  std::array<std::string, 4> paths = {
      "/usr/bin", "/usr/local/bin",
      std::getenv("VULKAN_SDK") ? std::string(std::getenv("VULKAN_SDK")) + "/Bin" : "",
      std::getenv("VULKAN_SDK") ? std::string(std::getenv("VULKAN_SDK")) + "/Bin32" : ""};

  for (const auto& path : paths) {
    std::filesystem::path validator_path = std::filesystem::path(path) / "glslangValidator";
    if (std::filesystem::exists(validator_path)) {
      return validator_path.string();
    }
  }

  fmt::println("Error: glslangValidator not found");
  exit(1);
}

void CompileShadersToSPIRV(const std::string& shader_dir, const std::string& glslang_validator_path,
                           bool force) {
  std::vector<std::filesystem::path> glsl_files;
  for (const auto& p : std::filesystem::recursive_directory_iterator(shader_dir)) {
    if (!IsShader(p.path())) continue;
    if (force ||
        !std::filesystem::exists(std::filesystem::path(p.path()).replace_extension(".spv"))) {
      glsl_files.emplace_back(p.path());
    }
  }

  for (const auto& glsl_file : glsl_files) {
    std::filesystem::path output_spv = glsl_file;
    output_spv.replace_extension(".spv");
    CompileToSPIRV(glslang_validator_path, glsl_file, output_spv);
  }
}

std::string GlslToSpvPath(const std::string& path) {
  return std::filesystem::path(path).replace_extension(".spv");
}
}  // namespace tvk::util
