#include <cstdlib>
#include <thread>

#include "ChunkMeshManager.hpp"
#include "MemUsage.hpp"
#include "Pool.hpp"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_keycode.h"
#include "Stats.hpp"
#include "Util.hpp"
#include "VoxelRenderer.hpp"
#include "application/CVar.hpp"
#include "application/Camera.hpp"
#include "application/Input.hpp"
#include "application/Renderer.hpp"
#include "application/Timer.hpp"
#include "application/Window.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "pch.hpp"
#include "voxels/Mesher.hpp"
#include "voxels/VoxelWorld.hpp"

namespace {

std::unique_ptr<VoxelWorld> world;
void InitWorld() {
  world = std::make_unique<VoxelWorld>();
  world->Init();
}
VoxelRenderer renderer;
Window window;
bool should_quit = false;
bool paused = false;
bool draw_imgui = true;
bool hide_mouse{false};
bool full_screen{false};
Camera main_cam;
vec3 sun_color{1, 1, 1};
vec3 sun_dir{0, -2, -1};
vec3 ambient_color{0.1, 0.1, 0.1};

AutoCVarFloat move_speed("camera.speed", "movement speed", 400.f, CVarFlags::EditFloatDrag);

float move_speed_vel{};
float move_speed_change_accel{0.2};
void UpdateCamera(double dt) {
  vec3 move{0};
  if (Input::IsKeyDown(SDLK_W) || Input::IsKeyDown(SDLK_I)) {
    move.x++;
  }
  if (Input::IsKeyDown(SDLK_S) || Input::IsKeyDown(SDLK_K)) {
    move.x--;
  }
  if (Input::IsKeyDown(SDLK_A) || Input::IsKeyDown(SDLK_J)) {
    move.z--;
  }
  if (Input::IsKeyDown(SDLK_D) || Input::IsKeyDown(SDLK_L)) {
    move.z++;
  }
  if (Input::IsKeyDown(SDLK_Y) || Input::IsKeyDown(SDLK_R)) {
    move.y++;
  }
  if (Input::IsKeyDown(SDLK_H) || Input::IsKeyDown(SDLK_F)) {
    move.y--;
  }
  if (Input::IsKeyDown(SDLK_EQUALS)) {
    move_speed_vel += move_speed_change_accel;
  } else if (Input::IsKeyDown(SDLK_MINUS)) {
    move_speed_vel -= move_speed_change_accel;
  } else {
    move_speed_vel = 0;
  }
  move_speed.Set(move_speed.Get() + move_speed_vel);
  auto dir = (main_cam.front * move.x) + (main_cam.right * move.z) + move.y * vec3(0, 1, 0);
  main_cam.position += dir * move_speed.GetFloat() * static_cast<float>(dt);
}

void OnEvent(const SDL_Event& e) {
  const auto& imgui_io = ImGui::GetIO();
  if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) &&
      imgui_io.WantCaptureKeyboard) {
    ImGui_ImplSDL3_ProcessEvent(&e);
    return;
  }
  if (e.type == SDL_EVENT_KEY_DOWN) {
    Input::SetKeyPressed(e.key.key, true);
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
      auto path = util::GetScreenshotPath(
          std::string(GET_PATH("local_screenshots" PATH_SEP "screenshot")), true);
      renderer.Screenshot(path);
      fmt::println("Saved screenshot to {}", path);
    } else if (sym == SDLK_F10) {
      InitWorld();
    }
  } else if (e.type == SDL_EVENT_KEY_UP) {
    Input::SetKeyPressed(e.key.key, false);
  } else if (e.type == SDL_EVENT_QUIT) {
    should_quit = true;
  } else if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
    paused = true;
  } else if (e.type == SDL_EVENT_WINDOW_HIDDEN) {
    paused = false;
  } else if ((e.type == SDL_EVENT_MOUSE_MOTION && hide_mouse)) {
    main_cam.UpdateRotation(e.motion.xrel, -e.motion.yrel);
  }

  ImGui_ImplSDL3_ProcessEvent(&e);
}

AutoCVarFloat fake_delay("misc.fake_delay", "Fake Delay", 0, CVarFlags::EditFloatDrag);

struct Stats {
  double tot_mesh_time{};
  size_t tot_meshes_made{};
  double frame_time{};
  void DrawImGui(double dt) const {
    static RollingAvgBuffer frame_times(10);
    frame_times.Add(dt);
    static float avg = frame_times.Avg();
    int interval = 12;
    static int i = 0;
    if (i++ > interval) {
      i = 0;
      avg = frame_times.Avg();
    }
    ImGui::Text("Meshes made total: %ld", tot_meshes_made);
    ImGui::Text("Avg mesh time %f us", tot_mesh_time / tot_meshes_made);
    ImGui::Text("Avg frame time %f ms / %f fps", avg, 1.f / avg);
    // static ImPlotAxisFlags flags = ImPlotAxisFlags_NoTickLabels;
    // static float t = 0;
    // t += dt;
    // frame_times.Add(t, frame_time);
    // static float history = 10.f;
    // ImGui::SliderFloat("History", &history, 1, 300, "%.1f s");
    // if (ImPlot::BeginPlot("##Scrolling", ImVec2(-1, 150))) {
    //   ImPlot::SetupAxes(nullptr, nullptr, flags, flags);
    //   ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
    //   ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1);
    //   ImPlot::SetNextFillStyle(IMPLOT_AUTO_COL, 0.5f);
    //   ImPlot::PlotShaded("Frame Times", &frame_times.data[0].x, &frame_times.data[0].y,
    //                      frame_times.data.size(), -INFINITY, 0, frame_times.offset,
    //                      sizeof(vec2));
    //   ImPlot::EndPlot();
    // }
    ImGui::Text("Curr RSS %ld MB, Peak %ld", getCurrentRSS() / 1024 / 1024, getPeakRSS());
    ImGui::Text("Real frame time %f ms / %f fps", frame_time, 1.f / frame_time);
  }

 private:
} stats;

void DrawImGui(double dt) {
  if (ImGui::Begin("Voxel Renderer")) {
    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
      world->DrawImGuiStats();
      stats.DrawImGui(dt);
      if (ImGui::TreeNodeEx("Chunk Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ChunkMeshManager::Get().DrawImGuiStats();
        ImGui::TreePop();
      }
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
      auto dir = main_cam.front;
      ImGui::Text("Direction %f %f %f", dir.x, dir.y, dir.z);
    }
  }
  ImGui::End();
}

void Update(double dt) {
  ZoneScoped;
  UpdateCamera(dt);
  world->Update();
}

using ChunkVertexVector = std::vector<uint64_t>;

constexpr int MaxMeshTasks = 256;
}  // namespace

int main() {
  FixedSizePtrPool<MeshAlgData> mesh_alg_pool;
  FixedSizePool<MesherOutputData> mesh_data_pool;
  mesh_alg_pool.Init(MaxMeshTasks);
  mesh_data_pool.Init(MaxMeshTasks);

  window.Init("Voxel Renderer", 1700, 900);
  renderer.Init(&window);
  InitWorld();

  main_cam.position = vec3(-700, 2250, -600);
  // main_cam.position = vec3(0, 0, 2);
  main_cam.LookAt({0, 0, 0});
  renderer.vsettings.aabb.min = vec3{-0.5};
  renderer.vsettings.aabb.max = vec3{0.5};

  Timer timer;
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
    if (fake_delay.Get() > 0.0000001f) {
      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(fake_delay.Get())));
    }
    Update(dt);

    if (draw_imgui) {
      ZoneScopedN("ImGui");
      window.StartImGuiFrame();
      DrawImGui(dt);
      window.EndImGuiFrame();
    }

    SceneData scene_data;
    scene_data.cam_dir = main_cam.front;
    scene_data.cam_pos = main_cam.position;
    scene_data.sun_color = sun_color;
    scene_data.sun_dir = sun_dir;
    scene_data.ambient_color = ambient_color;
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
    stats.frame_time = dt;
  }
  renderer.Cleanup();
}
