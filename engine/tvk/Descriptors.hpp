#pragma once
#include <vulkan/vulkan_core.h>

#include <deque>
#include <span>
namespace tvk {

struct DescriptorSetLayoutAndHash {
  VkDescriptorSetLayout layout;
  uint64_t hash;
};
struct DescriptorSetLayoutCache {
  void Init(VkDevice device);
  void Cleanup();
  void Clear();
  DescriptorSetLayoutAndHash CreateLayout(VkDevice device,
                                          const VkDescriptorSetLayoutCreateInfo& create_info);
  VkDescriptorSetLayout DummyLayout() const { return dummy_layout_; }

 private:
  VkDescriptorSetLayout dummy_layout_;
  std::unordered_map<uint64_t, VkDescriptorSetLayout> cache_;
  VkDevice device_;
};

struct DescriptorAllocator {
  struct PoolSizeRatio {
    VkDescriptorType type;
    float ratio;
  };
  VkDescriptorPool pool;
  void InitPool(VkDevice device, uint32_t max_sets, std::span<PoolSizeRatio> pool_ratios);
  // destroy all the descriptors created from the pool and go back to initial state
  void ClearDescriptors(VkDevice device) const;
  void DestroyPool(VkDevice device) const;

  VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout);
};

// resizable set of descriptor pools
struct DescriptorAllocatorGrowable {
  struct PoolSizeRatio {
    VkDescriptorType type;
    float ratio;
  };

  void Init(VkDevice device, uint32_t initial_sets, std::span<PoolSizeRatio> pool_ratios);
  void ClearPools(VkDevice device);
  void DestroyPools(VkDevice device);
  VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout, void* p_next = nullptr);

 private:
  VkDescriptorPool GetPool(VkDevice device);
  VkDescriptorPool CreatePool(VkDevice device, uint32_t set_count,
                              std::span<PoolSizeRatio> pool_ratios);

  std::vector<PoolSizeRatio> ratios_;
  std::vector<VkDescriptorPool> ready_pools_;
  std::vector<VkDescriptorPool> full_pools_;
  uint32_t sets_per_pool_;
};

struct AllocatedImage;
struct DescriptorBuilder {
  DescriptorBuilder& Begin(VkShaderStageFlags stages = 0);
  DescriptorBuilder& WriteImage(int binding, VkImageView image, VkSampler sampler,
                                VkImageLayout layout, VkDescriptorType type);
  DescriptorBuilder& WriteImage(int binding, VkImageView image, VkSampler sampler,
                                VkImageLayout layout, VkDescriptorType type,
                                VkShaderStageFlags stages);
  DescriptorBuilder& WriteImage(int binding, VkImageView image, VkImageLayout layout,
                                VkDescriptorType type);
  DescriptorBuilder& WriteGeneralStorageImage(int binding, const AllocatedImage& image);
  DescriptorBuilder& WriteBuffer(int binding, VkDescriptorBufferInfo* info, VkDescriptorType type);
  DescriptorBuilder& WriteBuffer(int binding, VkBuffer buffer, size_t size, size_t offset,
                                 VkDescriptorType type);
  DescriptorBuilder& WriteBuffer(int binding, VkBuffer buffer, size_t size, size_t offset,
                                 VkDescriptorType type, VkShaderStageFlags stages);
  [[nodiscard]] VkDescriptorSet Build(VkDevice device, DescriptorSetLayoutCache& cache,
                                      DescriptorAllocatorGrowable& allocator);

 private:
  VkShaderStageFlags stages_;
  std::vector<VkDescriptorSetLayoutBinding> bindings_;
  std::deque<VkDescriptorImageInfo> image_infos_;
  std::deque<VkDescriptorBufferInfo> buffer_infos_;
  std::vector<VkWriteDescriptorSet> writes_;
};

}  // namespace tvk
