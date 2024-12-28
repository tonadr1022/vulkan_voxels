#pragma once

namespace tvk::util {
std::string FindGlslangValidator();

bool IsShader(const std::string& path);
void CompileShadersToSPIRV(const std::string& shader_dir, const std::string& glslang_validator_path,
                           bool force);
void CompileToSPIRV(const std::string& glslangValidator, const std::string& glsl_path,
                    const std::string& output_path);
std::string GlslToSpvPath(const std::string& path);
}  // namespace tvk::util
