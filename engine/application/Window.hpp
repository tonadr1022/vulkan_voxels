#pragma once

#include "glm/vec2.hpp"
namespace vk {
struct DeletionQueue;
}
union SDL_Event;

class Window {
 public:
  void Init(const char* title, int width, int height);
  void Shutdown();
  void StartImGuiFrame() const;
  void EndImGuiFrame();
  [[nodiscard]] glm::ivec2 GetWindowSize() const;
  [[nodiscard]] float GetAspectRatio() const;
  glm::ivec2 UpdateWindowSize();
  void SetCursorPos(int x, int y);
  void CenterCursor();

  [[nodiscard]] struct SDL_Window* GetContext() const { return window_; }

 private:
  struct SDL_Window* window_{nullptr};
  int w_, h_;
};
