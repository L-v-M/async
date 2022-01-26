#include <cstdlib>
#include <fstream>
#include <numeric>
#include <vector>

#include "aesdragontamer.h"
#include "benchmark/benchmark.h"
#include "dram.h"
#include "lehmer64.h"
#include "ssd.h"
#include "wyhash.h"

using namespace dram;
using namespace ssd;

// Create a 512 GiB large file with: `dd if=/dev/zero of=file.dat bs=1GiB
// count=512`
const char *kFileName = "/raid0/merzljak/io/file.dat";

// Write output to /dev/null to prevent the compiler from optimizations
std::ofstream null{"/dev/null"};

static void BM_DRAMRandReadLatency(benchmark::State &state) {
  size_t num_cache_lines = kNumCacheLines32GiB;

  std::vector<CacheLine> data(num_cache_lines);

  // Make sure that the access pattern is random
  std::vector<size_t> indexes(num_cache_lines);
  std::iota(indexes.begin(), indexes.end(), 0ull);
  // Shuffle indexes
  std::random_device rd;
  std::mt19937 g(rd());
  g.seed(42);
  std::shuffle(indexes.begin(), indexes.end(), g);

  // Initialize data
  for (size_t i = 0, end = num_cache_lines - 1; i != end; ++i) {
    data[indexes[i]].next = &data[indexes[i + 1]];
  }
  data[indexes.back()].next = &data[indexes.front()];

  CacheLine *current = &data.front();
  for (auto _ : state) {
    current = current->next;
  }
  null << current;
}
BENCHMARK(BM_DRAMRandReadLatency);

static void SSDBench(benchmark::State &state, ssize_t page_size,
                     bool do_random_io) {
  File file{kFileName, File::kRead};
  size_t num_pages = file.file_size / page_size;
  std::vector<Entry> entries =
      InitializeEntries(num_pages, page_size, do_random_io);
  ConnectEntries(entries);

  void *buffer = std::aligned_alloc(page_size, page_size);

  Entry *current = &entries.front();
  for (auto _ : state) {
    Expect(pread(file.fd, buffer, page_size, current->offset) == page_size);
    current = current->next;
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

static void BM_AESDragontamer(benchmark::State &state) {
  constexpr uint64_t kPowerOfTwo = 1ull << 24;
  aesdragontamer_state aes_state;
  aesdragontamer_seed(&aes_state, 42);
  uint64_t sum = 0;
  for (auto _ : state) {
    sum += (aesdragontamer(&aes_state) % kPowerOfTwo);
  }
  null << sum;
}
BENCHMARK(BM_AESDragontamer);

static void BM_Wyhash(benchmark::State &state) {
  constexpr uint64_t kPowerOfTwo = 1ull << 24;
  uint64_t wyhash_state = 42;
  uint64_t sum = 0;
  for (auto _ : state) {
    sum += (wyhash64_stateless(&wyhash_state) % kPowerOfTwo);
  }
  null << sum;
}
BENCHMARK(BM_Wyhash);

static void BM_StdRandom(benchmark::State &state) {
  constexpr uint64_t kPowerOfTwo = 1ull << 24;
  std::random_device rd;   // obtain a random number from hardware
  std::mt19937 gen(rd());  // seed the generator
  std::uniform_int_distribution<> distr(0,
                                        kPowerOfTwo - 1);  // define the range
  uint64_t sum = 0;
  for (auto _ : state) {
    sum += distr(gen);
  }
  null << sum;
}
BENCHMARK(BM_StdRandom);

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