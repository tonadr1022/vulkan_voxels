#include "SamplerCache.hpp"

namespace {

size_t CombineHashes(size_t seed, size_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  return seed;
}
}  // namespace

namespace tvk::resource {

VkSampler SamplerCache::GetSampler(const VkSamplerCreateInfo& info) {
  ZoneScoped;
  std::hash<uint64_t> hasher;
  size_t hash = 0;
  hash = CombineHashes(hash, hasher(info.addressModeU));
  hash = CombineHashes(hash, hasher(info.addressModeV));
  hash = CombineHashes(hash, hasher(info.addressModeW));
  hash = CombineHashes(hash, hasher(info.minFilter));
  hash = CombineHashes(hash, hasher(info.magFilter));
  hash = CombineHashes(hash, hasher(info.anisotropyEnable));
  hash = CombineHashes(hash, hasher(info.maxAnisotropy));
  hash = CombineHashes(hash, hasher(info.flags));
  hash = CombineHashes(hash, hasher(info.compareEnable));
  hash = CombineHashes(hash, hasher(info.compareOp));
  auto it = sampler_cache_.find(hash);
  if (it != sampler_cache_.end()) {
    return it->second;
  }
  VkSampler sampler;
  vkCreateSampler(device_, &info, nullptr, &sampler);
  sampler_cache_.emplace(hash, sampler);
  return sampler;
}

void SamplerCache::Clear() {
  for (auto& [_, sampler] : sampler_cache_) {
    vkDestroySampler(device_, sampler, nullptr);
  }
}

namespace {
SamplerCache* instance{nullptr};
}  // namespace

void SamplerCache::Init(VkDevice device) {
  assert(!instance);
  instance = new SamplerCache;
  instance->device_ = device;
}

SamplerCache& SamplerCache::Get() { return *instance; }
}  // namespace tvk::resource
