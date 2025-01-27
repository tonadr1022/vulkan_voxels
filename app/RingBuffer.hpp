#pragma once

struct NonOwningRingBuffer {
  void Init(size_t size) {
    capacity_ = size;
    ptr_ = 0;
  }

  size_t Allocate(size_t size) {
    if (ptr_ + size >= capacity_) {
      ptr_ = size;
      return 0;
    }
    size_t pos = ptr_;
    ptr_ += size;
    return pos;
  }

 private:
  size_t capacity_;
  size_t ptr_{};
};

template <typename T>
struct RingBuffer {
  void Init(size_t count) { data.resize(count); }
  T* Allocate() {
    T* ret = &data[ptr_];
    ptr_++;
    if (ptr_ == data.size()) ptr_ = 0;
    return ret;
  }
  std::vector<T> data;

 private:
  size_t ptr_{};
};
