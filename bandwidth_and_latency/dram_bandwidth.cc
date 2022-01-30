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
              "kNumCacheLines must be a power of 2");

// Return the bandwidth in GB/s
double DoReads(std::span<CacheLine> cache_lines, size_t num_threads,
               size_t max_num_iterations_per_thread, bool do_random_io) {
  std::chrono::steady_clock::time_point start_time_point;
  std::chrono::steady_clock::time_point stop_time_point;

  // The barrier is used twice. The first time, the completion function sets the
  // start_time_point. The second time, it sets the stop_time_point.
  std::barrier barrier(num_threads,
                       [current = &start_time_point,
                        next = &stop_time_point]() mutable noexcept {
                         *current = std::chrono::steady_clock::now();
                         current = next;
                       });

  // After a thread has performed max_num_iterations_per_thread read operations,
  // it sets finished to true. All threads will stop immediately and write the
  // number of read operations they performed into iterations_per_thread.
  std::atomic<bool> finished{false};
  std::vector<size_t> iterations_per_thread(num_threads, 0ull);

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (size_t thread_idx = 0; thread_idx != num_threads; ++thread_idx) {
    threads.emplace_back([&barrier, cache_lines, &iterations_per_thread,
                          thread_idx, max_num_iterations_per_thread, &finished,
                          do_random_io, num_threads]() {
      if (do_random_io) {
        // To simulate random I/O, we use a fast random number generator:
        // https://lemire.me/blog/2019/03/19/the-fastest-conventional-random-number-generator-that-can-pass-big-crush/.
        // Each lehmer state gets a unique seed not used by any other thread.
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
          // cache_lines[i].payload.front() is 1
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
        // Let the threads start at different locations to minimize the
        // effectiveness of CPU caches
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
          // cache_lines[i].payload.front() is 1
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

void PrintCSVHeader() { std::cout << "access_pattern,num_threads,bandwidth\n"; }

void PrintResult(const char *access_pattern, size_t num_threads,
                 double bandwidth) {
  std::cout << access_pattern << "," << num_threads << "," << bandwidth << "\n";
}
}  // namespace

int main() {
  PrintCSVHeader();

  std::vector<CacheLine> cache_lines(kNumCacheLines);

  // Sequential reads
  for (size_t num_threads = 1; num_threads <= 128; ++num_threads) {
    // If kNumCacheLines == kNumCacheLines128GiB, the threads read 1TiB in total
    PrintResult("sequential", num_threads,
                DoReads(cache_lines, num_threads,
                        kNumCacheLines / num_threads * 8, false));
  }

  // Random reads
  for (size_t num_threads = 1; num_threads <= 128; ++num_threads) {
    // If kNumCacheLines == kNumCacheLines128GiB, the threads read 1TiB in total
    PrintResult("random", num_threads,
                DoReads(cache_lines, num_threads,
                        kNumCacheLines / num_threads * 8, true));
  }
}
