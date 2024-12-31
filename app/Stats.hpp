#pragma once

struct ScrollingPointBuffer {
  size_t max_size{};
  size_t offset{};
  std::vector<vec2> data;
  explicit ScrollingPointBuffer(size_t max_size = 1000) : max_size(max_size) {
    data.reserve(max_size);
  }
  void Add(float x, float y) {
    if (data.size() < max_size) {
      data.emplace_back(x, y);
    } else {
      data[offset] = {x, y};
      offset = (offset + 1) % max_size;
    }
  }
  void Erase() {
    data.clear();
    offset = 0;
  }
};

struct RollingAvgBuffer {
  std::vector<float> data;
  size_t max_size;
  size_t offset{};
  explicit RollingAvgBuffer(size_t size = 8) : max_size(size) { data.reserve(size); }
  void Add(float v) {
    if (data.size() < max_size) {
      data.emplace_back(v);
    } else {
      data[offset] = v;
      offset = (offset + 1) % max_size;
    }
  }
  [[nodiscard]] float Avg() const {
    float s{};
    for (auto f : data) {
      s += f;
    }
    return s / data.size();
  }
};
