#include <cmath>
#include <cstdint>
#include <iostream>
int main() {
  constexpr int MaxDepth = 5;
  int off_pow = std::pow(2, MaxDepth);
  constexpr int CS = 62;
  auto get_offset = [](int depth) { return (2 << (depth - 1)) * CS; };
  for (int i = MaxDepth; i >= 0; i--) {
    int curr = CS * off_pow;
    std::cout << curr << ",get " << get_offset(i) << '\n';
    off_pow /= 2;
  }

  // int cs = 62;
  // int i = 0;
  // int k = 1;
  // for (int j = 0; j < 5; j++) {
  //   std::cout << k * cs << '\n';
  //   k *= 2;
  // }
  // 0, CS, CS*2, CS * 4
}
