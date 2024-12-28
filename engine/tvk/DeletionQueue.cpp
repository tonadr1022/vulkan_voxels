#include "DeletionQueue.hpp"

#include <ranges>

namespace tvk {

void DeletionQueue::PushFunc(std::function<void()> &&func) { deletors_.emplace_back(func); }

void DeletionQueue::Flush() {
  for (auto &deletor : std::ranges::reverse_view(deletors_)) {
    deletor();
  }
  deletors_.clear();
}

void TimeDeletionQueue::UpdateFrame(uint64_t frame) { curr_frame_ = frame; }

void TimeDeletionQueue::PushFunc(std::function<void()> &&func, uint64_t frame) {
  deletors_.emplace_back(func, frame);
}

void TimeDeletionQueue::Flush() {
  auto it = deletors_.begin();
  while (curr_frame_ > it->frame && it != deletors_.end()) {
    it->deletor();
    it++;
  }
  deletors_.erase(deletors_.begin(), it);
}

}  // namespace tvk
