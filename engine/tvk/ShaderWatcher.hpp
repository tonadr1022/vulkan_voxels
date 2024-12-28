#pragma once

using RecompileShaderCallback = std::function<void(const std::string&)>;
struct ShaderWatcherData;

class ShaderWatcher {
 public:
  void StartWatching();
  void StopWatching();
  void BuildDependencyGraph(const std::string& shader_dir);
  ShaderWatcher() = default;
  void Init(const std::string& cache_dir, const std::string& shader_dir,
            const RecompileShaderCallback& recompile_shader_func, int sleep_time_ms);
  void Shutdown();

  ShaderWatcher(const ShaderWatcher& other) = delete;
  ShaderWatcher(ShaderWatcher&& other) = delete;
  ShaderWatcher& operator=(const ShaderWatcher& other) = delete;
  ShaderWatcher& operator=(ShaderWatcher&& other) = delete;
  ~ShaderWatcher();

 private:
  ShaderWatcherData* data_{nullptr};
  void Process();
};
