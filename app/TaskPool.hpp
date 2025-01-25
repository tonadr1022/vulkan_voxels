#pragma once

#include <concurrentqueue.h>

#include <queue>

template <typename T, typename D>
struct TaskPool {
  size_t in_flight{0};
  // moodycamel::ConcurrentQueue<T> to_complete_tasks;
  size_t to_complete_task_queue_size{};
  moodycamel::ConcurrentQueue<D> done_tasks;
  std::queue<T> to_complete;
  void Clear() {
    D cmp;
    while (to_complete_task_queue_size > 0) {
      if (!done_tasks.try_dequeue(cmp)) {
        break;
      }
      to_complete_task_queue_size--;
    }
    to_complete_task_queue_size = 0;
    in_flight = 0;
  }
};

template <typename T, typename D>
class TaskPool2 {
 public:
  // moodycamel::ConcurrentQueue<T> to_complete_tasks;
  size_t to_complete_task_queue_size{};
  moodycamel::ConcurrentQueue<D> done_tasks;
  std::queue<T> to_complete;
  void Clear() {
    D cmp;
    while (to_complete_task_queue_size > 0) {
      if (!done_tasks.try_dequeue(cmp)) {
        break;
      }
      to_complete_task_queue_size--;
    }
    to_complete_task_queue_size = 0;
    in_flight_ = 0;
  }

  [[nodiscard]] bool CanEnqueueTask() const { return in_flight_ < max_tasks_; }
  void Init(size_t max_tasks) { max_tasks_ = max_tasks; }
  void IncInFlight() { in_flight_++; }
  void DecInFlight() { in_flight_--; }
  [[nodiscard]] size_t InFlight() const { return in_flight_; }
  [[nodiscard]] size_t MaxTasks() const { return max_tasks_; }

 private:
  std::atomic<size_t> in_flight_{0};
  size_t max_tasks_{0};
};
