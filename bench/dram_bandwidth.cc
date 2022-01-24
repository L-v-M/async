#include <atomic>
#include <barrier>
#include <bit>
#include <chrono>
#include <iostream>
#include <numeric>
#include <span>
#include <thread>
#include <vector>

#include "dram.h"
#include "lehmer64.h"

using namespace dram;

namespace {

constexpr size_t kNumCacheLines = kNumCacheLines128GiB;
static_assert(std::has_single_bit(kNumCacheLines),
              "kNumCacheLines should be a power of 2");

// Returns the total bandwidth
double DoReads(std::span<CacheLine> cache_lines, size_t num_threads,
               size_t max_num_iterations_per_thread, bool do_random_io) {
  std::chrono::steady_clock::time_point start_time_point;
  std::chrono::steady_clock::time_point stop_time_point;
  // Use barrier to measure start and stop time points
  std::barrier barrier(num_threads,
                       [current = &start_time_point,
                        next = &stop_time_point]() mutable noexcept {
                         *current = std::chrono::steady_clock::now();
                         current = next;
                       });

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
  std::vector<size_t> iterations_per_thread(num_threads, 0ull);

  std::atomic<bool> finished{false};

  for (size_t thread_idx = 0; thread_idx != num_threads; ++thread_idx) {
    threads.emplace_back([&barrier, cache_lines, &iterations_per_thread,
                          thread_idx, max_num_iterations_per_thread, &finished,
                          do_random_io, num_threads]() {
      if (do_random_io) {
        lehmer64_state_t lehmer_state1;
        lehmer64_seed(&lehmer_state1, 7 * thread_idx);
        lehmer64_state_t lehmer_state2;
        lehmer64_seed(&lehmer_state2, 7 * thread_idx + 1);
        lehmer64_state_t lehmer_state3;
        lehmer64_seed(&lehmer_state3, 7 * thread_idx + 2);

        // Start the benchmark
        barrier.arrive_and_wait();

        size_t num_iterations1 = 0;
        size_t num_iterations2 = 0;
        size_t num_iterations3 = 0;
        for (size_t i = 0; i < max_num_iterations_per_thread && !finished;
             i += 3) {
          // cache_lines[index].payload.front() is 1
          num_iterations1 +=
              cache_lines[lehmer64(&lehmer_state1) % kNumCacheLines]
                  .payload.front();
          num_iterations2 +=
              cache_lines[lehmer64(&lehmer_state2) % kNumCacheLines]
                  .payload.front();
          num_iterations3 +=
              cache_lines[lehmer64(&lehmer_state3) % kNumCacheLines]
                  .payload.front();
        }

        // Signal the other threads to stop
        finished = true;
        // Finish the benchmark
        barrier.arrive_and_wait();

        iterations_per_thread[thread_idx] =
            num_iterations1 + num_iterations2 + num_iterations3;
      } else {
        // Let the threads start at different locations
        size_t cache_lines_per_thread =
            (kNumCacheLines + num_threads - 1) / num_threads;
        auto begin_idx = thread_idx * cache_lines_per_thread;

        // Start the benchmark
        barrier.arrive_and_wait();

        size_t num_iterations = 0;
        for (size_t i = begin_idx,
                    end = max_num_iterations_per_thread + begin_idx;
             i != end && !finished; ++i) {
          // compute the next index
          size_t index = i % kNumCacheLines;
          // cache_lines[index].payload.front() is 1
          num_iterations += cache_lines[index].payload.front();
        }

        // Signal the other threads to stop
        finished = true;
        // Finish the benchmark
        barrier.arrive_and_wait();

        iterations_per_thread[thread_idx] = num_iterations;
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  auto total_iterations = std::accumulate(iterations_per_thread.begin(),
                                          iterations_per_thread.end(), 0ull);

  std::chrono::nanoseconds nanos{stop_time_point - start_time_point};
  return total_iterations * sizeof(CacheLine) / double(nanos.count());
}

void PrintResult(const char *name, size_t num_threads, double bandwidth) {
  std::cout << name << "," << num_threads << "," << bandwidth << "\n";
}
}  // namespace

int main() {
  std::vector<CacheLine> cache_lines(kNumCacheLines);

  // Sequential reads
  for (size_t num_threads = 1; num_threads <= 128; ++num_threads) {
    // In total, the threads read 1 TiB
    PrintResult("Sequential", num_threads,
                DoReads(cache_lines, num_threads,
                        kNumCacheLines / num_threads * 8, false));
  }

  // Random reads
  for (size_t num_threads = 1; num_threads <= 128; ++num_threads) {
    // In total, the threads read 1 TiB
    PrintResult("Random", num_threads,
                DoReads(cache_lines, num_threads,
                        kNumCacheLines / num_threads * 8, true));
  }
}
