#ifdef BENCH
#include <bit>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "benchmark/benchmark.h"
#include "dram.h"
#include "lehmer64.h"
#include "ssd.h"

using namespace dram;
using namespace ssd;

// Create a 512 GiB large file with: `dd if=/dev/zero of=file.dat bs=1GiB
// count=512`
const char *kFileName = "/raid0/merzljak/io/file.dat";

// Write output to /dev/null to prevent certain compiler optimizations
std::ofstream null{"/dev/null"};

static void BM_DRAMRandReadLatency(benchmark::State &state) {
  constexpr size_t kNumCacheLines = kNumCacheLines128GiB;

  std::vector<CacheLine> data(kNumCacheLines);

  // Generate random sequence of indexes
  std::vector<size_t> indexes(kNumCacheLines);
  std::iota(indexes.begin(), indexes.end(), 0ull);
  // Shuffle indexes
  std::random_device rd;
  std::mt19937 g(rd());
  g.seed(42);
  std::shuffle(indexes.begin(), indexes.end(), g);

  // Initialize data
  for (size_t i = 0, end = kNumCacheLines - 1; i != end; ++i) {
    data[indexes[i]].next = &data[indexes[i + 1]];
  }
  data[indexes.back()].next = &data[indexes.front()];

  // Perform one (dependent) random read after another
  CacheLine *current = &data.front();
  for (auto _ : state) {
    current = current->next;
  }
  null << current;
}
BENCHMARK(BM_DRAMRandReadLatency);

static void Expect(bool predicate, const char *what = "Expect failed") {
  if (!predicate) {
    throw std::runtime_error{what};
  }
}

static void SSDBench(benchmark::State &state, ssize_t page_size,
                     bool do_random_io) {
  File file{kFileName};
  size_t num_pages = file.file_size / page_size;
  auto entries = InitializeEntries(num_pages, page_size, do_random_io);

  // The buffer must be aligned since we perform direct I/O
  void *buffer = std::aligned_alloc(page_size, page_size);

  size_t i = 0;

  for (auto _ : state) {
    Expect(pread(file.fd, buffer, page_size, entries[i % num_pages].offset) ==
           page_size);
    ++i;
  }

  std::free(buffer);
}

static void BM_SSDSeqReadLatency4KiB(benchmark::State &state) {
  ssize_t page_size = kPageSize4KiB;
  bool do_random_io = false;
  SSDBench(state, page_size, do_random_io);
}
BENCHMARK(BM_SSDSeqReadLatency4KiB);

static void BM_SSDSeqReadLatency64KiB(benchmark::State &state) {
  ssize_t page_size = kPageSize64KiB;
  bool do_random_io = false;
  SSDBench(state, page_size, do_random_io);
}
BENCHMARK(BM_SSDSeqReadLatency64KiB);

static void BM_SSDSeqReadLatency512KiB(benchmark::State &state) {
  ssize_t page_size = kPageSize512KiB;
  bool do_random_io = false;
  SSDBench(state, page_size, do_random_io);
}
BENCHMARK(BM_SSDSeqReadLatency512KiB);

static void BM_SSDRandReadLatency4KiB(benchmark::State &state) {
  ssize_t page_size = kPageSize4KiB;
  bool do_random_io = true;
  SSDBench(state, page_size, do_random_io);
}
BENCHMARK(BM_SSDRandReadLatency4KiB);

static void BM_SSDRandReadLatency64KiB(benchmark::State &state) {
  ssize_t page_size = kPageSize64KiB;
  bool do_random_io = true;
  SSDBench(state, page_size, do_random_io);
}
BENCHMARK(BM_SSDRandReadLatency64KiB);

static void BM_SSDRandReadLatency512KiB(benchmark::State &state) {
  ssize_t page_size = kPageSize512KiB;
  bool do_random_io = true;
  SSDBench(state, page_size, do_random_io);
}
BENCHMARK(BM_SSDRandReadLatency512KiB);

static void BM_Lehmer(benchmark::State &state) {
  constexpr uint64_t kPowerOfTwo = 1ull << 24;
  lehmer64_state_t lehmer_state;
  lehmer64_seed(&lehmer_state, 42);

  uint64_t sum = 0;
  for (auto _ : state) {
    sum += (lehmer64(&lehmer_state) % kPowerOfTwo);
  }
  null << sum;
}
BENCHMARK(BM_Lehmer);

static void BM_Lehmer3(benchmark::State &state) {
  constexpr uint64_t kPowerOfTwo = 1ull << 24;
  lehmer64_state_t lehmer_state1;
  lehmer64_seed(&lehmer_state1, 1);
  lehmer64_state_t lehmer_state2;
  lehmer64_seed(&lehmer_state2, 3);
  lehmer64_state_t lehmer_state3;
  lehmer64_seed(&lehmer_state3, 5);

  uint64_t sum1 = 0;
  uint64_t sum2 = 0;
  uint64_t sum3 = 0;
  for (auto _ : state) {
    sum1 += (lehmer64(&lehmer_state1) % kPowerOfTwo);
    sum2 += (lehmer64(&lehmer_state2) % kPowerOfTwo);
    sum3 += (lehmer64(&lehmer_state3) % kPowerOfTwo);
  }
  null << sum1 + sum2 + sum3;
}
BENCHMARK(BM_Lehmer3);

BENCHMARK_MAIN();
#endif