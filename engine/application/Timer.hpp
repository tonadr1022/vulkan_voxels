#pragma once

#include <chrono>

class Timer {
 public:
  Timer() { Start(); }
  ~Timer() = default;

  void Start() { start_time_ = std::chrono::high_resolution_clock::now(); };

  double ElapsedSeconds() { return ElapsedMicro() * 0.000001; }

  double ElapsedMS() { return ElapsedMicro() * 0.001; }

  uint64_t ElapsedMicro() {
    auto end_time = std::chrono::high_resolution_clock::now();
    start_ = time_point_cast<std::chrono::microseconds>(start_time_).time_since_epoch().count();
    end_ = time_point_cast<std::chrono::microseconds>(end_time).time_since_epoch().count();
    return end_ - start_;
  }

  void Print() { fmt::println("ElapsedMS: {}", ElapsedMS()); }
  void PrintMicro() { fmt::println("ElapsedMicro: {}", ElapsedMicro()); }
  void Reset(std::string_view msg) {
    Print(msg);
    Reset();
  }
  void Print(std::string_view msg) { fmt::println("{}: {}", msg, ElapsedMS()); }
  void Reset() { start_time_ = std::chrono::high_resolution_clock::now(); }

 private:
  uint64_t start_, end_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
};

class PrintTimerMicro : public Timer {
 public:
  ~PrintTimerMicro() { PrintMicro(); }
};
