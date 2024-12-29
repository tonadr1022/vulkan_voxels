#pragma once

constexpr const int CS = 62;
constexpr const int HALFCS = CS >> 2;
constexpr const int CS2 = CS * CS;
constexpr const int CS3 = CS2 * CS;

constexpr const int PCS = 64;
constexpr const int PCS2 = CS * CS;
constexpr const int PCS3 = CS2 * CS;

using ChunkGrid = std::array<uint8_t, static_cast<size_t>(PCS3)>;
using OpaqueMask = std::array<uint64_t, PCS2>;
constexpr uint32_t ZXY(int x, int y, int z) { return z + (x * CS) + (y * CS2); }
constexpr uint32_t XYZ(int x, int y, int z) { return x + (y * CS) + (z * CS2); }
// Chunk volume size
inline void Set(ChunkGrid& grid, int x, int y, int z, uint8_t value) { grid[ZXY(x, y, z)] = value; }
inline uint8_t Get(ChunkGrid& grid, int x, int y, int z) { return grid[ZXY(x, y, z)]; }
