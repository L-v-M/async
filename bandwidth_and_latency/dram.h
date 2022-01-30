#ifndef BANDWIDTH_AND_LATENCY_DRAM_H_
#define BANDWIDTH_AND_LATENCY_DRAM_H_

#include <array>

namespace dram {

constexpr size_t kSizeOfCacheLine = 64ull;

struct alignas(kSizeOfCacheLine) CacheLine {
  CacheLine* next;
  const std::array<size_t, 7> payload{1, 1, 1, 1, 1, 1, 1};
};

static_assert(sizeof(CacheLine) == kSizeOfCacheLine &&
              alignof(CacheLine) == kSizeOfCacheLine);

constexpr size_t kNumCacheLines1GiB = (1ull << 30) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines2GiB = (1ull << 31) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines4GiB = (1ull << 32) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines8GiB = (1ull << 33) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines16GiB = (1ull << 34) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines32GiB = (1ull << 35) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines64GiB = (1ull << 36) / kSizeOfCacheLine;
constexpr size_t kNumCacheLines128GiB = (1ull << 37) / kSizeOfCacheLine;

}  // namespace dram

#endif