#pragma once

template <typename T>
struct PtrObjPool {
  void Init(uint32_t size) {
    data.resize(size);
    free_list.resize(size);
    InitInternal();
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

  size_t Size() { return allocs; }

  void Clear() {
    data.clear();
    data.resize(free_list.size());
    InitInternal();
  }
  void ClearNoDealloc() { InitInternal(); }

  size_t allocs{};
  std::vector<std::unique_ptr<T>> data;
  std::vector<uint32_t> free_list;

 private:
  void InitInternal() {
    allocs = {};
    for (uint32_t i = 0; i < free_list.size(); i++) {
      free_list[i] = i;
    }
  }
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
    capacity_ = size;
    data.resize(size, {});
    free_list_.resize(size);
    InitInternal();
  }
  size_t Size() { return size_; }
  size_t Capacity() { return capacity_; }

  T* Alloc() {
    if (free_head_ == -1) {
      return nullptr;
    }
    uint32_t curr = free_head_;
    free_head_ = free_list_[free_head_];
    size_++;
    return &data[curr];
  }

  void Free(T* obj) {
    assert(obj >= data.data() && obj < data.data() + data.size());
    uint32_t idx = obj - data.data();
    free_list_[idx] = free_head_;
    free_head_ = idx;
    size_--;
  }

  void Clear() {
    size_ = {};
    data.clear();
    data.resize(free_list_.size(), {});
    InitInternal();
  }

  void ClearNoDealloc() {
    size_ = {};
    InitInternal();
  }

  std::vector<T> data;

 private:
  size_t size_{};
  size_t capacity_;
  int32_t free_head_ = -1;
  std::vector<uint32_t> free_list_;

  void InitInternal() {
    for (size_t i = 0; i < free_list_.size(); i++) {
      free_list_[i] = i + 1;
    }
    // end of free list
    free_list_[free_list_.size() - 1] = -1;
    // first free slot
    free_head_ = 0;
  }
};

// template <typename T>
// struct UniversalPool {
//   UniversalPool(uint32_t cnt, bool owns_memory, uint32_t num_blocks = 10000)
//       : block_capacity_(num_blocks), owns_memory_(owns_memory) {
//     if (owns_memory) {
//       memstart_ = new T[cnt];
//     }
//   }
//   struct Block {
//     uint32_t position;
//     uint32_t size;
//   };
//   uint32_t Alloc() {}
//
//   void Free(uint32_t handle) {}
//
//   T* Get(uint32_t handle) { return memstart_ + handle; }
//   T* Start() { return memstart_; }
//
//  private:
//   T* memstart_;
//   uint32_t block_capacity_;
//   bool owns_memory_;
// };
