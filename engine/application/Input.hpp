#pragma once
#include <map>

#include "SDL3/SDL_keycode.h"

struct Input {
 public:
  static bool IsKeyDown(SDL_Keycode key) { return key_states[key]; }
  static bool IsMouseButtonPressed(int button) { return mouse_button_states[button]; }
  static void SetKeyPressed(SDL_Keycode key, bool pressed) { key_states[key] = pressed; };
  static void SetMouseButtonPressed(uint32_t button, bool pressed) {
    mouse_button_states[button] = pressed;
  }

 private:
  static inline std::map<SDL_Keycode, bool> key_states;
  static inline std::map<uint32_t, bool> mouse_button_states;
};
