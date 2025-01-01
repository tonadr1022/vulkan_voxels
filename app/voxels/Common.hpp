#pragma once

constexpr const int CS = 62;
constexpr const int HALFCS = CS >> 2;
constexpr const int CS2 = CS * CS;
constexpr const int CS3 = CS2 * CS;

constexpr const int PCS = 64;
constexpr const int PCS2 = PCS * PCS;
constexpr const int PCS3 = PCS2 * PCS;

template <i8vec3 Len>
using Grid3Du8 = std::array<uint8_t, static_cast<std::size_t>(Len.x* Len.y* Len.z)>;

template <i8vec3 Len>
using HeightMapFloats = std::array<float, static_cast<std::size_t>(Len.x* Len.z)>;

template <i8vec3 Len>
using HeightMapGrid = std::array<int, static_cast<std::size_t>(Len.x* Len.z)>;

using ChunkPaddedHeightMapFloats = HeightMapFloats<i8vec3{PCS}>;
using ChunkPaddedHeightMapGrid = HeightMapGrid<i8vec3{PCS}>;

using OpaqueMask = std::array<uint64_t, PCS2>;

template <i8vec3 Len>
constexpr uint32_t XZY(int x, int y, int z) {
  return x + (z * Len.x) + (y * Len.x * Len.z);
}

template <i8vec3 Len>
constexpr uint32_t ZXY(int x, int y, int z) {
  return z + (x * Len.z) + (y * Len.x * Len.z);
}

template <int Len>
constexpr uint32_t XYZ(int x, int y, int z) {
  return x + (y * Len) + (z * Len * Len);
}

template <i8vec3 Len>
inline void SetXZY(Grid3Du8<Len>& grid, int x, int y, int z, uint8_t value) {
  grid[XZY<Len>(x, y, z)] = value;
}
template <i8vec3 Len>
inline uint8_t GetXZY(const Grid3Du8<Len>& grid, int x, int y, int z) {
  return grid[XZY<Len>(x, y, z)];
}
template <i8vec3 Len>
inline void SetZXY(Grid3Du8<Len>& grid, int x, int y, int z, uint8_t value) {
  grid[ZXY<Len>(x, y, z)] = value;
}

template <i8vec3 Len>
inline uint8_t GetZXY(const Grid3Du8<Len>& grid, int x, int y, int z) {
  return grid[ZXY<Len>(x, y, z)];
}
