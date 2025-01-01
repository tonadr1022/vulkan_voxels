#pragma once

template <typename T>
struct PtrObjPool {
  void Init(uint32_t size) {
    data.resize(size);
    free_list.reserve(size);
    for (uint32_t i = 0; i < size; i++) {
      free_list.emplace_back(i);
    }
  }

  uint32_t Alloc() {
    if (free_list.empty()) {
      fmt::println("Resizing pool, new size: {}", data.size() * 1.5);
      auto old_size = data.size();
      data.resize(data.size() * 1.5);
      auto additional = data.size() - old_size;
      free_list.reserve(free_list.size() + additional);
      for (size_t i = old_size; i < data.size(); i++) {
        free_list.emplace_back(i);
      }
    }
    auto idx = free_list.back();
    free_list.pop_back();
    if (data[idx] == nullptr) {
      data[idx] = std::make_unique<T>();
    }
    allocs++;
    return idx;
  }
  T* Get(uint32_t handle) {
    assert(handle < data.size());
    return data[handle].get();
  }

  void Free(uint32_t handle) {
    allocs--;
    free_list.emplace_back(handle);
  }

  size_t allocs;
  std::vector<std::unique_ptr<T>> data;
  std::vector<uint32_t> free_list;
};

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
    allocs--;
    return idx;
  }
  T* Get(uint32_t handle) {
    assert(handle < data.size());
    allocs++;
    return data[handle].get();
  }

  void Free(uint32_t handle) { free_list.emplace_back(handle); }

  std::vector<std::unique_ptr<T>> data;
  std::vector<uint32_t> free_list;
  size_t allocs{};
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
    allocs++;
    return &data[curr];
  }

  void Free(T* obj) {
    assert(obj >= data.data() && obj < data.data() + data.size());
    uint32_t idx = obj - data.data();
    free_list[idx] = free_head;
    free_head = idx;
    allocs--;
  }

  size_t allocs{};
  int32_t free_head = -1;
  std::vector<uint32_t> free_list;
  std::vector<T> data;
};