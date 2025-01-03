#include "Octree.hpp"

#include <cstdint>

namespace {
uint8_t GetChildIdx(uvec3 pos, uint8_t curr_depth) {
  return (pos.y >> (Octree::MaxDepth - curr_depth - 1) & 1) << 2 |
         (pos.x >> (Octree::MaxDepth - curr_depth - 1) & 1) << 1 |
         (pos.z >> (Octree::MaxDepth - curr_depth - 1) & 1);
}

template <typename T>
void DumpBits(T d, size_t size = sizeof(T)) {
  static_assert(std::is_integral_v<T>, "T must be an integral type.");
  for (int i = static_cast<int>(size) - 1; i >= 0; i--) {
    fmt::print("{}", ((d & (static_cast<T>(1) << i)) ? 1 : 0));
  }
  fmt::println("");
}

}  // namespace

void Octree::Init() {
  auto* d = new uint32_t[10];
  for (int i = 0; i < 10; i++) {
    d[i] = 10;
    fmt::println("i {}", d[i]);
  }
  exit(1);
  // nodes_[0].AllocNode();
  // uvec3 iter{};
  // auto cnt = 2u;
  // uint32_t val = 1;
  // int i = 0;
  // for (iter.y = 0; iter.y < cnt; iter.y++) {
  //   for (iter.x = 0; iter.x < cnt; iter.x++) {
  //     for (iter.z = 0; iter.z < cnt; iter.z++, i++) {
  //       SetVoxel(iter, 2, val + i);
  //       EASSERT(GetVoxel(iter) == val + i);
  //     }
  //   }
  // }
  // val = 0;
  // SetVoxel({0, 0, 0}, 1, val);
  // for (iter.y = 0; iter.y < cnt; iter.y++) {
  //   for (iter.x = 0; iter.x < cnt; iter.x++) {
  //     for (iter.z = 0; iter.z < cnt; iter.z++) {
  //       EASSERT(GetVoxel(iter) == val);
  //     }
  //   }
  // }
}

void Octree::SetVoxel(uvec3 pos, uint32_t depth, VoxelData val) {
  uint32_t curr_depth = 0;
  // get root node
  Node* curr_node = nodes_[0].Get(0);
  while (curr_depth < depth) {
    uint8_t child_idx = GetChildIdx(pos, curr_depth);
    // make node if needed
    if (curr_node->IsLeaf(child_idx)) {
      uint32_t idx = nodes_[curr_depth + 1].AllocNode();
      curr_node->SetChild(child_idx, idx);
    }

    // traverse to child
    curr_node = nodes_[curr_depth + 1].Get(curr_node->data[child_idx]);
    curr_depth++;
  }

  uint32_t final_child_idx = GetChildIdx(pos, curr_depth);
  if (!curr_node->IsLeaf(final_child_idx)) {
    FreeChildNodes(curr_node->data[final_child_idx], curr_depth + 1);
  }
  curr_node->SetData(final_child_idx, val);
}

uint32_t Octree::GetVoxel(uvec3 pos) {
  uint32_t curr_depth = 0;
  Node* curr_node = nodes_[0].Get(0);
  while (curr_depth < MaxDepth) {
    uint8_t idx = GetChildIdx(pos, curr_depth);
    if (curr_node->IsLeaf(idx)) {
      return curr_node->data[idx];
    }
    curr_node = nodes_[curr_depth].Get(curr_node->data[idx]);
    curr_depth++;
  }
  EASSERT(0 && "unreachable");
}

void Octree::FreeChildNodes(uint32_t node_idx, uint32_t depth) {
  if (depth >= MaxDepth) return;
  Node* node = nodes_[depth].Get(node_idx);
  for (uint8_t i = 0; i < 8; i++) {
    if (!node->IsLeaf(i)) {
      uint32_t child_idx = node->data[i];
      FreeChildNodes(child_idx, depth + 1);
      nodes_[depth + 1].Free(child_idx);
    }
  }
  nodes_[depth].Free(node_idx);
}
