#pragma once

class Window;

namespace tvk {

struct RendererImpl;
struct Renderer {
  void Init(Window* window);
  void Draw();
  void Cleanup();
  void DrawImGui();
  void ReloadShaders();
  void Screenshot(const std::string& path);

  ~Renderer();

 private:
  RendererImpl* impl_;
};

}  // namespace tvk
