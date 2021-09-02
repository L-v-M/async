#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include "cppcoro/sync_wait.hpp"
#include "cppcoro/task.hpp"
#include "cppcoro/when_all_ready.hpp"
#include "storage/file.h"
#include "storage/io_uring.h"

using Type = std::uint64_t;
constexpr std::uint64_t kPageSize = 1ull << 25;
constexpr std::uint64_t kNumValuesPerPage = kPageSize / sizeof(Type);
constexpr std::uint64_t kNumPages = 1ull << 7;
constexpr std::uint64_t kNumRingEntries = 1ull << 5;
static_assert(kNumRingEntries <= kNumPages);
constexpr std::uint64_t kNumValues = kNumPages * kNumValuesPerPage;
constexpr std::uint64_t kSizeInBytes = kNumValues * sizeof(Type);
constexpr bool kShouldGenerateData = false;
constexpr bool kShouldUseDirectIO = true;
constexpr bool kShouldComputeSum = true;

template <typename T>
class AlignedUniquePtr {
 public:
  explicit AlignedUniquePtr(std::size_t alignment, std::size_t size)
      : ptr_(reinterpret_cast<T*>(std::aligned_alloc(alignment, size)),
             &std::free) {}

  T* get() const noexcept { return ptr_.get(); }

 private:
  std::unique_ptr<T, decltype(&std::free)> ptr_;
};

void SequentialRead(storage::File& file) {
  AlignedUniquePtr<Type> data{kPageSize, kSizeInBytes};
  auto start = std::chrono::high_resolution_clock::now();
  file.ReadBlock(reinterpret_cast<std::byte*>(data.get()), 0, kSizeInBytes);
  std::uint64_t sum = 0;
  if constexpr (kShouldComputeSum) {
    sum = std::accumulate(data.get(), data.get() + kNumValues, Type{0});
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  std::cout << "Processed "
            << ((kSizeInBytes) / 1'000'000'000.0) / (milliseconds / 1'000.0)
            << " GB/s - Result: " << sum << "\n";
}

void SequentialReadPageWise(storage::File& file) {
  AlignedUniquePtr<Type> data{kPageSize, kPageSize};
  auto start = std::chrono::high_resolution_clock::now();
  std::uint64_t sum = 0;
  for (std::uint64_t i = 0; i != kNumPages; ++i) {
    file.ReadBlock(reinterpret_cast<std::byte*>(data.get()), i * kPageSize,
                   kPageSize);
    if constexpr (kShouldComputeSum) {
      sum +=
          std::accumulate(data.get(), data.get() + kNumValuesPerPage, Type{0});
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  std::cout << "Processed "
            << ((kSizeInBytes) / 1'000'000'000.0) / (milliseconds / 1'000.0)
            << " GB/s - Result: " << sum << "\n";
}

class Countdown {
 public:
  explicit Countdown(std::uint64_t counter) noexcept : counter_(counter) {}

  void Decrement() noexcept { --counter_; }

  bool IsZero() const noexcept { return counter_ == 0; }

 private:
  std::atomic<std::uint64_t> counter_;
};

cppcoro::task<Type> AsyncSum(storage::File& file, std::uint64_t begin,
                             std::uint64_t end, Type* data,
                             storage::IOUring& ring, Countdown& countdown) {
  Type sum = 0;
  for (; begin != end; ++begin) {
    co_await file.AsyncReadBlock(ring, reinterpret_cast<std::byte*>(data),
                                 begin * kPageSize, kPageSize);
    if constexpr (kShouldComputeSum) {
      sum += std::accumulate(data, data + kNumValuesPerPage, Type{0});
    }
  }
  countdown.Decrement();
  co_return sum;
}

cppcoro::task<Type> DrainRing(storage::IOUring& ring,
                              const Countdown& countdown) {
  while (!countdown.IsZero()) {
    ring.ProcessBatch();
  }
  co_return Type{0};
}

void AsyncRead(storage::File& file) {
  storage::IOUring ring(kNumRingEntries);
  Countdown countdown(kNumRingEntries);
  constexpr std::uint64_t kNumPagesPerCoroutine = kNumPages / kNumRingEntries;
  std::vector<AlignedUniquePtr<Type>> data;
  data.reserve(kNumRingEntries);
  for (std::uint64_t i = 0; i != kNumRingEntries; ++i) {
    data.emplace_back(kPageSize, kPageSize);
  }
  auto start = std::chrono::high_resolution_clock::now();
  std::vector<cppcoro::task<Type>> tasks;
  tasks.reserve(kNumRingEntries);
  for (std::uint64_t i = 0; i != kNumRingEntries; ++i) {
    tasks.emplace_back(AsyncSum(file, i * kNumPagesPerCoroutine,
                                (i + 1) * kNumPagesPerCoroutine, data[i].get(),
                                ring, countdown));
  }
  tasks.emplace_back(DrainRing(ring, countdown));
  auto results = cppcoro::sync_wait(cppcoro::when_all_ready(std::move(tasks)));
  std::uint64_t sum = 0;
  if constexpr (kShouldComputeSum) {
    for (auto& result : results) {
      sum += result.result();
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  std::cout << "Processed "
            << ((kSizeInBytes) / 1'000'000'000.0) / (milliseconds / 1'000.0)
            << " GB/s - Result: " << sum << "\n";
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " data.dat\n";
    return 1;
  }
  const char* file_name = argv[1];
  if (kShouldGenerateData) {
    storage::File file{file_name, storage::File::kWrite};
    std::vector<Type> data(kNumValues);
    std::iota(data.begin(), data.end(), Type{1});
    constexpr std::uint64_t kNumValuesPerWrite = kNumValues / 4;
    for (auto i = 0; i != 4; ++i) {
      file.AppendBlock(reinterpret_cast<const std::byte*>(
                           data.data() + i * kNumValuesPerWrite),
                       kNumValuesPerWrite * sizeof(Type));
    }
  }
  storage::File file{file_name, storage::File::kRead, kShouldUseDirectIO};
  SequentialRead(file);
  SequentialReadPageWise(file);
  AsyncRead(file);
}