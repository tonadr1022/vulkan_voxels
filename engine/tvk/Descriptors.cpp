#include "Descriptors.hpp"

#include <vulkan/vulkan_core.h>

#include "Error.hpp"
#include "Resource.hpp"

namespace tvk {

namespace {

size_t CombineHashes(size_t seed, size_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  return seed;
}
uint64_t HashDescriptorSetLayoutCreateInfo(const VkDescriptorSetLayoutCreateInfo& info) {
  uint64_t hash = std::hash<uint32_t>{}(info.flags);
  std::hash<uint32_t> hasher;
  hash = CombineHashes(hash, hasher(info.flags));
  for (uint32_t i = 0; i < info.bindingCount; i++) {
    const auto& binding = info.pBindings[i];
    hash = CombineHashes(hash, hasher(binding.binding));
    hash = CombineHashes(hash, hasher(binding.stageFlags));
    hash = CombineHashes(hash, hasher(binding.descriptorCount));
    hash = CombineHashes(hash, hasher(binding.descriptorType));
  }
  return hash;
}
}  // namespace

void DescriptorAllocator::InitPool(VkDevice device, uint32_t max_sets,
                                   std::span<PoolSizeRatio> pool_ratios) {
  std::vector<VkDescriptorPoolSize> pool_sizes;
  for (PoolSizeRatio ratio : pool_ratios) {
    pool_sizes.emplace_back(ratio.type, static_cast<uint32_t>(ratio.ratio * max_sets));
  }
  VkDescriptorPoolCreateInfo pool_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .pNext = nullptr,
                                       .flags = 0,
                                       .maxSets = max_sets,
                                       .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
                                       .pPoolSizes = pool_sizes.data()};
  vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}
void DescriptorAllocator::ClearDescriptors(VkDevice device) const {
  vkResetDescriptorPool(device, pool, 0);
}
void DescriptorAllocator::DestroyPool(VkDevice device) const {
  vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::Allocate(VkDevice device, VkDescriptorSetLayout layout) {
  VkDescriptorSetAllocateInfo alloc_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .pNext = nullptr,
                                         .descriptorPool = pool,
                                         .descriptorSetCount = 1,
                                         .pSetLayouts = &layout};
  VkDescriptorSet ds;
  VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, &ds));
  return ds;
}

void DescriptorAllocatorGrowable::Init(VkDevice device, uint32_t initial_sets,
                                       std::span<PoolSizeRatio> pool_ratios) {
  ratios_.clear();
  for (auto&& r : pool_ratios) {
    ratios_.emplace_back(r);
  }
  VkDescriptorPool new_pool = CreatePool(device, initial_sets, pool_ratios);
  sets_per_pool_ = initial_sets * 1.5;
  ready_pools_.emplace_back(new_pool);
}

void DescriptorAllocatorGrowable::ClearPools(VkDevice device) {
  for (auto& p : ready_pools_) {
    vkResetDescriptorPool(device, p, 0);
  }
  for (auto& p : full_pools_) {
    vkResetDescriptorPool(device, p, 0);
    ready_pools_.emplace_back(p);
  }
  full_pools_.clear();
}

void DescriptorAllocatorGrowable::DestroyPools(VkDevice device) {
  for (auto& p : ready_pools_) {
    vkDestroyDescriptorPool(device, p, nullptr);
  }
  ready_pools_.clear();
  for (auto& p : full_pools_) {
    vkDestroyDescriptorPool(device, p, nullptr);
  }
  full_pools_.clear();
}

VkDescriptorSet DescriptorAllocatorGrowable::Allocate(VkDevice device, VkDescriptorSetLayout layout,
                                                      void* p_next) {
  // get a descriptor pool
  VkDescriptorPool pool_to_use = GetPool(device);
  VkDescriptorSetAllocateInfo alloc_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                         .pNext = p_next,
                                         .descriptorPool = pool_to_use,
                                         .descriptorSetCount = 1,
                                         .pSetLayouts = &layout};
  VkDescriptorSet ds;
  // allocate a descriptor set in the pool
  VkResult result = vkAllocateDescriptorSets(device, &alloc_info, &ds);

  // if pool out of room, get a new pool and send failed pool to full
  if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
    full_pools_.emplace_back(pool_to_use);
    pool_to_use = GetPool(device);
    alloc_info.descriptorPool = pool_to_use;
    // should not fail anymore
    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, &ds));
  }

  ready_pools_.emplace_back(pool_to_use);
  return ds;
}

VkDescriptorPool DescriptorAllocatorGrowable::GetPool(VkDevice device) {
  VkDescriptorPool new_pool;
  if (!ready_pools_.empty()) {
    new_pool = ready_pools_.back();
    ready_pools_.pop_back();
  } else {
    new_pool = CreatePool(device, sets_per_pool_, ratios_);
    sets_per_pool_ *= 1.5;
    sets_per_pool_ = std::min<uint32_t>(sets_per_pool_, 4092);
  }

  return new_pool;
}

VkDescriptorPool DescriptorAllocatorGrowable::CreatePool(VkDevice device, uint32_t set_count,
                                                         std::span<PoolSizeRatio> pool_ratios) {
  std::vector<VkDescriptorPoolSize> pool_sizes;
  for (PoolSizeRatio ratio : pool_ratios) {
    pool_sizes.emplace_back(ratio.type, static_cast<uint32_t>(ratio.ratio * set_count));
  }
  VkDescriptorPoolCreateInfo pool_info{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .pNext = nullptr,
                                       .flags = 0,
                                       .maxSets = set_count,
                                       .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
                                       .pPoolSizes = pool_sizes.data()};
  VkDescriptorPool new_pool;
  vkCreateDescriptorPool(device, &pool_info, nullptr, &new_pool);
  return new_pool;
}

DescriptorBuilder& DescriptorBuilder::WriteImage(int binding, VkImageView image, VkSampler sampler,
                                                 VkImageLayout layout, VkDescriptorType type) {
  return WriteImage(binding, image, sampler, layout, type, stages_);
}

DescriptorBuilder& DescriptorBuilder::WriteBuffer(int binding, VkDescriptorBufferInfo* info,
                                                  VkDescriptorType type) {
  // assert(0 && " broken");
  VkDescriptorSetLayoutBinding new_bind{};
  new_bind.binding = binding;
  new_bind.descriptorCount = 1;
  new_bind.descriptorType = type;
  new_bind.stageFlags = stages_;
  bindings_.emplace_back(new_bind);

  // pointer stability provided by deque
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = nullptr;
  write.dstBinding = binding;
  write.dstSet = VK_NULL_HANDLE;  // no set until we need to write to it
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pBufferInfo = info;
  writes_.emplace_back(write);
  return *this;
}

DescriptorBuilder& DescriptorBuilder::WriteBuffer(int binding, VkBuffer buffer, size_t size,
                                                  size_t offset, VkDescriptorType type) {
  return WriteBuffer(binding, buffer, size, offset, type, stages_);
}

DescriptorBuilder& DescriptorBuilder::WriteImage(int binding, VkImageView image, VkSampler sampler,
                                                 VkImageLayout layout, VkDescriptorType type,
                                                 VkShaderStageFlags stages) {
  // add the binding
  VkDescriptorSetLayoutBinding new_bind{};
  new_bind.binding = binding;
  new_bind.descriptorCount = 1;
  new_bind.descriptorType = type;
  new_bind.stageFlags = stages;
  bindings_.emplace_back(new_bind);

  // make the write
  VkDescriptorImageInfo& info = image_infos_.emplace_back(
      VkDescriptorImageInfo{.sampler = sampler, .imageView = image, .imageLayout = layout});
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = nullptr;
  write.dstBinding = binding;
  write.dstSet = VK_NULL_HANDLE;  // no set until we need to write to it
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pImageInfo = &info;
  writes_.emplace_back(write);
  return *this;
}

DescriptorBuilder& DescriptorBuilder::WriteBuffer(int binding, VkBuffer buffer, size_t size,
                                                  size_t offset, VkDescriptorType type,
                                                  VkShaderStageFlags stages) {
  VkDescriptorSetLayoutBinding new_bind{};
  new_bind.binding = binding;
  new_bind.descriptorCount = 1;
  new_bind.descriptorType = type;
  new_bind.stageFlags = stages;
  bindings_.emplace_back(new_bind);

  // pointer stability provided by deque
  VkDescriptorBufferInfo& info = buffer_infos_.emplace_back(
      VkDescriptorBufferInfo{.buffer = buffer, .offset = offset, .range = size});
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = nullptr;
  write.dstBinding = binding;
  write.dstSet = VK_NULL_HANDLE;  // no set until we need to write to it
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pBufferInfo = &info;
  writes_.emplace_back(write);
  return *this;
}

DescriptorBuilder& DescriptorBuilder::Begin(VkShaderStageFlags stages) {
  stages_ = stages;
  bindings_.clear();
  writes_.clear();
  image_infos_.clear();
  buffer_infos_.clear();
  return *this;
}

VkDescriptorSet DescriptorBuilder::Build(VkDevice device, DescriptorSetLayoutCache& cache,
                                         DescriptorAllocatorGrowable& allocator) {
  VkDescriptorSetLayoutCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.pNext = nullptr;
  info.pBindings = bindings_.data();
  info.bindingCount = static_cast<uint32_t>(bindings_.size());
  // TODO: flags
  info.flags = 0;

  auto layout = cache.CreateLayout(device, info);

  VkDescriptorSet set = allocator.Allocate(device, layout.layout, nullptr);
  for (VkWriteDescriptorSet& write : writes_) {
    write.dstSet = set;
  }
  vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes_.size()), writes_.data(), 0, nullptr);
  return set;
}

DescriptorSetLayoutAndHash DescriptorSetLayoutCache::CreateLayout(
    VkDevice device, const VkDescriptorSetLayoutCreateInfo& create_info) {
  ZoneScoped;
  auto hash = HashDescriptorSetLayoutCreateInfo(create_info);
  auto it = cache_.find(hash);
  if (it != cache_.end()) {
    return {it->second, it->first};
  }
  VkDescriptorSetLayout layout;
  VK_CHECK(vkCreateDescriptorSetLayout(device, &create_info, nullptr, &layout));
  cache_.emplace(hash, layout);
  return {layout, hash};
}

void DescriptorSetLayoutCache::Init(VkDevice device) {
  device_ = device;
  VkDescriptorSetLayoutCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  create_info.bindingCount = 0;
  create_info.pBindings = nullptr;
  VK_CHECK(vkCreateDescriptorSetLayout(device_, &create_info, nullptr, &dummy_layout_));
}
void DescriptorSetLayoutCache::Cleanup() {
  vkDestroyDescriptorSetLayout(device_, dummy_layout_, nullptr);
  Clear();
}
void DescriptorSetLayoutCache::Clear() {
  for (auto& [hash, layout] : cache_) {
    vkDestroyDescriptorSetLayout(device_, layout, nullptr);
  }
  cache_.clear();
}
DescriptorBuilder& DescriptorBuilder::WriteImage(int binding, VkImageView image,
                                                 VkImageLayout layout, VkDescriptorType type) {
  return WriteImage(binding, image, VK_NULL_HANDLE, layout, type);
}
DescriptorBuilder& DescriptorBuilder::WriteGeneralStorageImage(int binding,
                                                               const AllocatedImage& image) {
  return WriteImage(binding, image.view, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}
}  // namespace tvk
