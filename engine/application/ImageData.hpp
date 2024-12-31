#pragma once

struct ImageData {
  int w, h, channels;
  int row_pitch;
  void* data;
};
