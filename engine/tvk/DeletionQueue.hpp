#pragma once

#include <deque>
#include <functional>

namespace tvk {

struct DeletionQueue {
  void PushFunc(std::function<void()> &&func);
  void Flush();

 private:
  std::deque<std::function<void()>> deletors_;
};

// TODO: move to src file
struct TimeDeletionQueue {
  struct Entry {
    std::function<void()> deletor;
    uint64_t frame{0};
  };

  void UpdateFrame(uint64_t frame);
  void PushFunc(std::function<void()> &&func, uint64_t frame);
  void Flush();

 private:
  uint64_t curr_frame_;
  std::deque<Entry> deletors_;
};

}  // namespace tvk
