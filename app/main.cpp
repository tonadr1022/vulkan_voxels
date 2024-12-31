#include <cstddef>
#include <thread>

#include "ChunkMeshManager.hpp"
#include "SDL3/SDL_events.h"
#include "Util.hpp"
#include "VoxelRenderer.hpp"
#include "application/CVar.hpp"
#include "application/Camera.hpp"
#include "application/Renderer.hpp"
#include "application/Timer.hpp"
#include "application/Window.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "pch.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Grid3D.hpp"
#include "voxels/Mesher.hpp"
#include "voxels/Terrain.hpp"

namespace {

// Track allocations
size_t allocated_mem = 0;
size_t alloc_count = 0;

}  // namespace

// Standard new and delete
void* operator new(size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  allocated_mem += size;
  alloc_count++;
  return ptr;
}

void* operator new[](size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  allocated_mem += size;
  alloc_count++;
  return ptr;
}

void operator delete(void* ptr) noexcept {
  if (!ptr) return;
  alloc_count--;
  std::free(ptr);
}

void operator delete(void* ptr, size_t size) noexcept {
  if (!ptr) return;
  allocated_mem -= size;
  alloc_count--;
  std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
  if (!ptr) return;
  alloc_count--;
  std::free(ptr);
}

void operator delete[](void* ptr, size_t size) noexcept {
  if (!ptr) return;
  allocated_mem -= size;
  alloc_count--;
  std::free(ptr);
}

namespace {

VoxelRenderer renderer;
Window window;
bool should_quit = false;
bool paused = false;
bool draw_imgui = true;
bool hide_mouse{false};
bool full_screen{false};
Camera main_cam;

void OnEvent(const SDL_Event& e) {
  const auto& imgui_io = ImGui::GetIO();
  if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) &&
      imgui_io.WantCaptureKeyboard) {
    // ImGui_ImplSDL3_ProcessEvent(&e);
    return;
  }
  if (e.type == SDL_EVENT_KEY_DOWN) {
    auto sym = e.key.key;
    if (sym == SDLK_G && e.key.mod & SDL_KMOD_ALT) {
      draw_imgui = !draw_imgui;
    } else if (sym == SDLK_ESCAPE) {
      hide_mouse = !hide_mouse;
      if (!hide_mouse) {
        auto window_dims = window.GetWindowSize();
        window.SetCursorPos(window_dims.x / 2.f, window_dims.y / 2.f);
        SDL_SetWindowRelativeMouseMode(window.GetContext(), false);
        SDL_HideCursor();
      } else {
        SDL_SetWindowRelativeMouseMode(window.GetContext(), true);
        SDL_ShowCursor();
      }
    } else if (sym == SDLK_EQUALS) {
      main_cam.move_speed_mult++;
    } else if (sym == SDLK_MINUS) {
      main_cam.move_speed_mult--;
      main_cam.move_speed_mult = std::max(main_cam.move_speed_mult, 0.f);
    } else if (sym == SDLK_R && e.key.mod & SDL_KMOD_ALT) {
      renderer.ReloadShaders();
    } else if (sym == SDLK_F11) {
      full_screen = !full_screen;
      SDL_SetWindowFullscreen(window.GetContext(), full_screen & SDL_WINDOW_FULLSCREEN);
    } else if (sym == SDLK_F5) {
      auto path = util::GetScreenshotPath(std::string(GET_PATH("screenshot")), true);
      renderer.Screenshot(path);
      fmt::println("Saved screenshot to {}", path);
    }
  } else if (e.type == SDL_EVENT_QUIT) {
    should_quit = true;
  } else if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
    paused = true;
  } else if (e.type == SDL_EVENT_WINDOW_HIDDEN) {
    paused = false;
  }

  if (e.type != SDL_EVENT_MOUSE_MOTION || hide_mouse) {
    main_cam.OnEvent(e);
  }
  ImGui_ImplSDL3_ProcessEvent(&e);
}

struct Stats {
  double frame_time{};
  void DrawImGui() const {
    ImGui::Text("Memory usage: %zu Kb", allocated_mem / 1024);
    ImGui::Text("Memory Allocations: %zu Kb", alloc_count);
    ImGui::Text("Frame Time: %f", frame_time);
  }
} stats;

void DrawImGui() {
  if (ImGui::Begin("Voxel Renderer")) {
    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
      stats.DrawImGui();
    }
    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
      CVarSystem::Get().DrawImGuiEditor();
    }
    if (ImGui::CollapsingHeader("Renderer")) {
      renderer.DrawImGui();
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Position %f %f %f", main_cam.position.x, main_cam.position.y,
                  main_cam.position.z);
      auto dir = main_cam.GetLookDirection();
      ImGui::Text("Direction %f %f %f", dir.x, dir.y, dir.z);
    }
    ImGui::End();
  }
}

void Update(double) {
  ZoneScoped;
  main_cam.Update();
}

using ChunkVertexVector = std::vector<uint64_t>;

template <typename T>
struct FixedSizePtrPool {
  void Init(uint32_t size) {
    data.resize(size);
    free_list.reserve(size);
    for (uint32_t i = 0; i < size; i++) {
      free_list.emplace_back(i);
    }
  }

  uint32_t Alloc() {
    assert(!free_list.empty());
    auto idx = free_list.back();
    free_list.pop_back();
    if (data[idx] == nullptr) {
      data[idx] = std::make_unique<T>();
    }
    return idx;
  }
  T* Get(uint32_t handle) {
    assert(handle < data.size());
    return data[handle].get();
  }

  void Free(uint32_t handle) { free_list.emplace_back(handle); }

  std::vector<std::unique_ptr<T>> data;
  std::vector<uint32_t> free_list;
};

template <typename T>
struct FixedSizePool {
  void Init(uint32_t size) {
    data.resize(size, {});
    free_list.resize(size);
    for (uint32_t i = 0; i < size; i++) {
      free_list[i] = i + 1;
    }
    // end of free list
    free_list[size - 1] = -1;
    // first free slot
    free_head = 0;
  }

  T* Alloc() {
    if (free_head == -1) {
      return nullptr;
    }
    uint32_t curr = free_head;
    free_head = free_list[free_head];
    return &data[curr];
  }

  void Free(T* obj) {
    assert(obj >= data.data() && obj < data.data() + data.size());
    uint32_t idx = obj - data.data();
    free_list[idx] = free_head;
    free_head = idx;
  }

  int32_t free_head = -1;
  std::vector<uint32_t> free_list;
  std::vector<T> data;
};

constexpr int MaxMeshTasks = 64;
}  // namespace

int main() {
  FixedSizePtrPool<MeshAlgData> mesh_alg_pool;
  FixedSizePool<MeshData> mesh_data_pool;
  mesh_alg_pool.Init(MaxMeshTasks);
  mesh_data_pool.Init(MaxMeshTasks);

  window.Init("Voxel Renderer", 1700, 900);
  renderer.Init(&window);
  Timer timer;
  PaddedChunkGrid3D grid;
  // gen::FillSphere(grid.grid, grid.mask);

  // int seed = 1;
  // ChunkPaddedHeightMapGrid heights;
  // ChunkPaddedHeightMapFloats height_map_floats;
  // gen::FBMNoise fbm_noise(seed);
  // fbm_noise.FillNoise<i8vec3{PCS}>(height_map_floats, {});
  // gen::NoiseToHeights(height_map_floats, heights, {0, 32});

  // gen::FillChunk(grid, heights, 1);
  gen::FillSolid(grid, 1);
  MeshData* data = mesh_data_pool.Alloc();
  auto alg_data_alloc = mesh_alg_pool.Alloc();

  MeshAlgData* alg_data = mesh_alg_pool.Get(alg_data_alloc);
  data->mask = &grid.mask;

  assert(grid.ValidateBitmask());
  GenerateMesh(grid.grid.grid, *alg_data, *data);
  ivec3 chunk_pos = {0, 0, 0};
  std::vector<ChunkMeshManager::ChunkMeshUpload> uploads;
  if (data->vertex_cnt) {
    for (int i = 0; i < 6; i++) {
      if (alg_data->face_vertex_lengths[i]) {
        uint32_t base_instance =
            (i << 24) | (chunk_pos.z << 16) | (chunk_pos.y << 16) | (chunk_pos.x);
        ChunkMeshManager::ChunkMeshUpload u;
        u.count = alg_data->face_vertex_lengths[i];
        fmt::println("base_instance {}, count {}", base_instance, u.count);
        u.first_instance = base_instance;
        u.data = &data->vertices[alg_data->face_vertices_start_indices[i]];
        uploads.emplace_back(u);
      }
    }
    ChunkMeshManager::Get().UploadChunkMeshes(uploads);
  }

  mesh_data_pool.Free(data);
  mesh_alg_pool.Free(alg_data_alloc);

  // mesh

  main_cam.position = vec3(0, 0, 2);
  main_cam.LookAt({0, 0, 0});
  renderer.vsettings.aabb.min = vec3{-0.5};
  renderer.vsettings.aabb.max = vec3{0.5};
  double last_time = timer.ElapsedMS();
  while (!should_quit) {
    ZoneScopedN("Frame");
    double time = timer.ElapsedMS();
    double dt = (time - last_time) / 1000.f;
    last_time = time;
    {
      ZoneScopedN("Events");
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        OnEvent(e);
      }
    }

    if (paused) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    Update(dt);

    if (draw_imgui) {
      ZoneScopedN("ImGui");
      window.StartImGuiFrame();
      DrawImGui();
      window.EndImGuiFrame();
    }

    SceneData scene_data;
    scene_data.cam_dir = main_cam.GetLookDirection();
    scene_data.cam_pos = main_cam.position;
    scene_data.time = time;
    renderer.Draw(&scene_data, draw_imgui);
    // static int i = 0;
    // if (i++ < 10) {
    //   std::vector<ChunkMeshManager::ChunkMeshUpload> uploads;
    //   int cnt = 100;
    //   uploads.reserve(cnt);
    //   for (int j = 0; j < cnt; j++) {
    //     uploads.emplace_back(data->vertex_cnt, 0, data->vertices.data());
    //   }
    //   ChunkMeshManager::Get().UploadChunkMeshes(uploads);
    //   stats.frame_time = dt;
    // }
  }
  renderer.Cleanup();
}
