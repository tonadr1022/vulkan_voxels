#include "EAssert.hpp"

void HandleAssert(const char *msg, const char *condition, const char *filename,
                  uint64_t lineNumber) {
  fmt::println("Assert failed: {}\nCondition: {}\nFile: {}\nLine: {}", msg, condition, filename,
               lineNumber);
}
