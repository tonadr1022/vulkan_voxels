#include "Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "SDL3/SDL_video.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

void Window::Init(const char* title, int width, int height) {
  SDL_Init(SDL_INIT_VIDEO);
  window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!window_) {
    fmt::println("Failed to initialize window");
    exit(1);
  }
  UpdateWindowSize();
}

// void Window::ProcessEvents(const std::function<void(SDL_Event&)>& func) {
//   SDL_Event e;
//   while (SDL_PollEvent(&e)) {
//     func(e);
//   }
// }

glm::ivec2 Window::UpdateWindowSize() {
  SDL_GetWindowSize(GetContext(), &w_, &h_);
  return {w_, h_};
}

glm::ivec2 Window::GetWindowSize() const { return {w_, h_}; }

void Window::EndImGuiFrame() { ImGui::Render(); }

void Window::StartImGuiFrame() const {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void Window::Shutdown() { SDL_DestroyWindow(window_); }

void Window::SetCursorPos(int x, int y) { SDL_WarpMouseInWindow(window_, x, y); }

void Window::CenterCursor() {
  auto dims = GetWindowSize();
  SetCursorPos(dims.x / 2.f, dims.y / 2.f);
}
float Window::GetAspectRatio() const {
  auto dims = GetWindowSize();
  return static_cast<float>(dims.x) / static_cast<float>(dims.y);
}
