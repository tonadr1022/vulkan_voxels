
#include <iostream>
int main() {
  for (int qo = 0; qo < 5; qo++) {
    std::cout << "bitoffset: " << qo * 40 << " , wordoffest: " << qo * 40 / 32
              << " , bit start: " << (qo * 40) % 32 << '\n';
  }
  std::cout << (258 << 32) << '\n';
}
