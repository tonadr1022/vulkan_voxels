#include "ShaderWatcher.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <stack>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "Util.hpp"

// #define PRINT_DEPENDENCY_GRAPH

namespace {

constexpr const char* CacheFilename = "last_shader_write_cache.txt";

}  // namespace

struct ShaderWatcherData {
  std::unordered_map<std::string, std::filesystem::file_time_type> last_write_times;
  std::unordered_map<std::string, std::unordered_set<std::string>> dependencies;
  std::thread thread;
  std::atomic<bool> running{true};
  std::mutex mutex;
  std::condition_variable cv;
  int sleep_time_ms{1};
  std::string cache_dir;
  std::string shader_dir;
  std::unordered_set<std::string> to_recompile;
  bool need_rebuild_graph{true};
  RecompileShaderCallback recompile_shader_func;
};

void ShaderWatcher::Init(const std::string& cache_dir, const std::string& shader_dir,
                         const RecompileShaderCallback& recompile_shader_func, int sleep_time_ms) {
  data_ = new ShaderWatcherData{};
  data_->recompile_shader_func = recompile_shader_func;
  data_->cache_dir = cache_dir;
  data_->shader_dir = shader_dir;
  data_->sleep_time_ms = sleep_time_ms;
  std::ifstream ifs(std::filesystem::path(data_->cache_dir) / CacheFilename);
  if (ifs.is_open()) {
    std::string file;
    std::uint64_t timestamp;
    while (ifs >> file >> timestamp) {
      data_->last_write_times[file] =
          std::filesystem::file_time_type(std::chrono::nanoseconds(timestamp));
    }
  }

  Process();
}

namespace {
bool ParseInclude(const std::string& line, std::string& name) {
  size_t first_char = line.find_first_not_of(' ');
  if (first_char >= line.size()) {
    return false;
  }
  if (line[first_char] != '#') {
    return false;
  }
  size_t include_idx = line.find("#include", first_char);
  if (include_idx >= line.size()) {
    return false;
  }
  size_t quote_idx = line.find('\"', include_idx);
  size_t end_quote_idx = line.find('\"', quote_idx + 1);
  if (end_quote_idx < line.size()) {
    name = line.substr(quote_idx + 1, end_quote_idx - quote_idx - 1);
    size_t last_slash = name.find_last_of('/');
    if (last_slash < name.size()) {
      name = name.substr(last_slash + 1);
    }
    return true;
  }
  return false;
}
}  // namespace

void ShaderWatcher::BuildDependencyGraph(const std::string& shader_dir) {
  // fmt::println("Building dep graph");
  data_->dependencies.clear();
  std::unordered_set<std::string> parsed_files;
  std::stack<std::string> files_to_parse;

  auto parse_dependencies = [this, &shader_dir, &parsed_files,
                             &files_to_parse](const std::string& shader_path) {
    // fmt::println("parsing: {}", shader_path);

    std::ifstream file(shader_path);

    if (!file.is_open()) {
      fmt::println("File failed to open: {}", shader_path);
      return;
    }

    std::string line;
    std::string include_name;
    while (std::getline(file, line)) {
      if (ParseInclude(line, include_name)) {
        std::string included_path = (std::filesystem::path(shader_dir) / include_name).string();
        data_->dependencies[included_path].emplace(shader_path);

        if (!parsed_files.contains(included_path)) {
          parsed_files.emplace(included_path);
          files_to_parse.emplace(included_path);
        }
      }
    }
  };

  for (const auto& entry : std::filesystem::recursive_directory_iterator(shader_dir)) {
    if (tvk::util::IsShader(entry.path()) || entry.path().extension() == ".h") {
      std::string shader_path = entry.path().string();
      parsed_files.emplace(shader_path);
      files_to_parse.emplace(shader_path);
      while (!files_to_parse.empty()) {
        std::string next_file_to_parse = files_to_parse.top();
        files_to_parse.pop();
        parse_dependencies(next_file_to_parse);
      }
    }
  }
#ifdef PRINT_DEPENDENCY_GRAPH
  for (const auto& [file, dependencies] : data_->dependencies) {
    fmt::println("\n File: {}", file);
    for (const auto& dep : dependencies) {
      fmt::println("Dep: {}", dep);
    }
  }
#endif
}

namespace {

bool IsShader2(const std::filesystem::path& path) {
  return path.has_extension() && (path.extension() == ".glsl" || path.extension() == ".hlsl");
}

}  // namespace
void ShaderWatcher::Process() {
  if (data_->need_rebuild_graph) {
    data_->need_rebuild_graph = false;
    BuildDependencyGraph(data_->shader_dir);
  }
  std::function<void(const std::filesystem::path&)> process =
      [&, this](const std::filesystem::path& dir) -> void {
    // iterate the files
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
      const auto& path = entry.path();
      if (entry.is_directory()) {
        process(entry.path());
        continue;
      }
      // bool is_shader = util::IsShader(path);
      bool is_shader = IsShader2(path);
      if (is_shader || (path.has_extension() && path.extension() == ".h")) {
        auto it = data_->last_write_times.find(path);
        auto last_write_time = std::filesystem::last_write_time(path);
        if (it == data_->last_write_times.end() || it->second != last_write_time ||
            (tvk::util::IsShader(path) &&
             !std::filesystem::exists(std::filesystem::path(path).replace_extension(".spv")))) {
          data_->last_write_times[path] = last_write_time;
          data_->need_rebuild_graph = true;
          data_->to_recompile.emplace(path);
          for (const std::string& dep : data_->dependencies[path]) {
            data_->to_recompile.emplace(dep);
          }
        }
      }
    }
  };
  process(data_->shader_dir);
  if (data_->need_rebuild_graph) {
    data_->need_rebuild_graph = false;
    BuildDependencyGraph(data_->shader_dir);
  }
  for (const auto& shader : data_->to_recompile) {
    if (tvk::util::IsShader(shader)) data_->recompile_shader_func(shader);
  }
  data_->to_recompile.clear();
}

void ShaderWatcher::StartWatching() {
  Process();
  data_->thread = std::thread([this]() {
    while (true) {
      std::unique_lock<std::mutex> lock(data_->mutex);
      data_->cv.wait_for(lock, std::chrono::milliseconds(data_->sleep_time_ms),
                         [this]() { return !data_->running; });
      if (!data_->running) break;
      Process();
    }
  });
}

ShaderWatcher::~ShaderWatcher() { StopWatching(); }

void ShaderWatcher::StopWatching() {
  {
    std::lock_guard<std::mutex> lock(data_->mutex);
    data_->running = false;
  }
  data_->cv.notify_all();
  if (data_->thread.joinable()) {
    data_->thread.join();
  }
  if (!std::filesystem::exists(data_->cache_dir)) {
    std::filesystem::create_directories(data_->cache_dir);
  }
  std::ofstream ofs(std::filesystem::path(data_->cache_dir) / CacheFilename);
  if (ofs.is_open()) {
    for (const auto& [file, time] : data_->last_write_times) {
      ofs << file << ' ' << static_cast<size_t>(time.time_since_epoch().count()) << '\n';
    }
  }
}
