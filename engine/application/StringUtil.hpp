#pragma once

// source: https://github.com/vblanco20-1/vulkan-guide/blob/engine/extra-engine/string_utils.h

namespace util {

// FNV-1a 32bit hashing algorithm.
constexpr uint32_t fnv1a_32(char const* s, std::size_t count) {
  return ((count ? fnv1a_32(s, count - 1) : 2166136261u) ^ s[count]) * 16777619u;
}

namespace string {

constexpr size_t StrLen(const char* s) {
  size_t size = 0;
  while (s[size]) {
    size++;
  }
  return size;
}

struct Hash {
  uint32_t hash_value;
  constexpr Hash(uint32_t hash) noexcept : hash_value(hash) {}
  constexpr Hash(const char* s) noexcept : hash_value(0) { hash_value = fnv1a_32(s, StrLen(s)); }
  constexpr Hash(const char* s, std::size_t cnt) noexcept : hash_value(0) {
    hash_value = fnv1a_32(s, cnt);
  }
  constexpr Hash(std::string_view s) noexcept : hash_value(0) {
    hash_value = fnv1a_32(s.data(), s.size());
  }

  constexpr operator uint32_t() const noexcept { return hash_value; }
};

}  // namespace string

}  // namespace util
