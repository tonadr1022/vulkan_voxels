#pragma once

void HandleAssert(const char* msg, const char* condition, const char* filename,
                  uint64_t lineNumber);

#ifndef NDEBUG
#define USE_ASSERT
#endif
#define USE_ASSERT

#ifdef USE_ASSERT
#define EASSERT_MSG(stmt, msg)                      \
  do {                                              \
    if (!(stmt)) {                                  \
      HandleAssert(msg, #stmt, __FILE__, __LINE__); \
      std::abort();                                 \
    }                                               \
  } while (0)

#define EASSERT(stmt)                                              \
  do {                                                             \
    if (!(stmt)) {                                                 \
      HandleAssert("Assertion Failed", #stmt, __FILE__, __LINE__); \
      std::abort();                                                \
    }                                                              \
  } while (0)

#else

#define EASSERT_MSG(stmt, msg) \
  do {                         \
  } while (0)

#define EASSERT(stmt) \
  do {                \
  } while (0)

#endif
