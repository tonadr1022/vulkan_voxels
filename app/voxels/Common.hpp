#pragma once

#define PACK_QUAD

constexpr const int CS = 62;
constexpr const int HALFCS = CS / 2;
constexpr const int CS2 = CS * CS;
constexpr const int CS3 = CS2 * CS;

constexpr const int PCS = 64;
constexpr const int HALFPCS = PCS / 2;
constexpr const int PCS2 = PCS * PCS;
constexpr const int PCS3 = PCS2 * PCS;

template <int Len>
using Grid3Du8 = std::array<uint8_t, static_cast<std::size_t>(Len* Len* Len)>;

// template <i8vec3 Len>
using HeightMapFloats = std::array<float, static_cast<std::size_t>(PCS2)>;

template <int Len>
using FloatArray3D = std::array<float, static_cast<std::size_t>(Len* Len* Len)>;

template <int Len>
using HeightMapGrid = std::array<int, static_cast<std::size_t>(Len* Len)>;

using ChunkPaddedHeightMapFloats = HeightMapFloats;
using ChunkPaddedHeightMapGrid = HeightMapGrid<PCS>;

struct HeightMapData {
  HeightMapData() = default;
  HeightMapGrid<PCS> heights;
  ivec2 range;
};

using OpaqueMask = std::array<uint64_t, PCS2>;

template <int Len>
constexpr uint32_t XZY(int x, int y, int z) {
  return x + (z * Len) + (y * Len * Len);
}

template <int Len>
constexpr uint32_t ZXY(int x, int y, int z) {
  return z + (x * Len) + (y * Len * Len);
}

template <int Len>
constexpr uint32_t XYZ(int x, int y, int z) {
  return x + (y * Len) + (z * Len * Len);
}

template <int Len>
inline void SetXZY(Grid3Du8<Len>& grid, int x, int y, int z, uint8_t value) {
  grid[XZY<Len>(x, y, z)] = value;
}
template <int Len>
inline uint8_t GetXZY(const Grid3Du8<Len>& grid, int x, int y, int z) {
  return grid[XZY<Len>(x, y, z)];
}
template <int Len>
inline void SetZXY(Grid3Du8<Len>& grid, int x, int y, int z, uint8_t value) {
  grid[ZXY<Len>(x, y, z)] = value;
}

template <int Len>
inline uint8_t GetZXY(const Grid3Du8<Len>& grid, int x, int y, int z) {
  return grid[ZXY<Len>(x, y, z)];
}

namespace std {
template <>
struct hash<std::pair<int, int>> {
  std::size_t operator()(const std::pair<int, int>& pair) const {
    std::size_t h1 = std::hash<int>{}(pair.first);
    std::size_t h2 = std::hash<int>{}(pair.second);
    return h1 ^ (h2 << 1);  // Combine hashes
  }
};
}  // namespace std
