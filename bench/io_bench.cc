#include "io_bench.h"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace benchmark;

namespace {
constexpr size_t kSystemPageSize = 1ull << 12;
constexpr std::chrono::steady_clock::duration kBenchmarkDuration =
    std::chrono::seconds{45};

size_t DoWork(std::span<Entry> entries, auto &barrier, const File &file,
              const std::chrono::steady_clock::time_point &start_time_point,
              unsigned io_depth, size_t page_size) {
  auto raw_buffers =
      AllocateAlignedBuffers(io_depth, kSystemPageSize, page_size);
  IOUring ring{io_depth};
  std::vector<std::byte *> buffers;
  for (auto &buffer : raw_buffers) {
    buffers.push_back(buffer.get());
  }

  ConnectEntries(entries);

  // Start the benchmark
  barrier.arrive_and_wait();

  size_t num_completed_io_operations =
      ring.DoBenchmark(file, buffers, &entries.front(), page_size,
                       start_time_point + kBenchmarkDuration);

  // End the benchmark
  barrier.arrive_and_wait();
  return num_completed_io_operations;
}

void PrintResults(std::span<size_t> num_completed_io_operations_per_thread,
                  std::chrono::steady_clock::time_point start_time_point,
                  std::chrono::steady_clock::time_point stop_time_point,
                  bool do_random_io, unsigned num_threads,
                  unsigned io_depth_per_thread, size_t page_size) {
  std::chrono::nanoseconds nanos{stop_time_point - start_time_point};

  auto num_io_operations =
      std::accumulate(num_completed_io_operations_per_thread.begin(),
                      num_completed_io_operations_per_thread.end(), 0ull);

  if (do_random_io) {
    std::cout << "Random,";
  } else {
    std::cout << "Sequential,";
  }
  double bandwidth = num_io_operations * double(page_size) / nanos.count();
  std::cout << num_threads << "," << io_depth_per_thread << "," << page_size
            << "," << bandwidth << "\n";
}
}  // namespace

int main(int argc, const char *argv[]) {
  if (argc != 2) {
    // Create the file with: `dd if=/dev/zero of=file.dat bs=1GiB count=512`
    std::cerr << "Usage: " << argv[0] << " file.dat\n";
    return 1;
  }

  std::vector<size_t> page_sizes{1ull << 12, 1ull << 14, 1ull << 16, 1ull << 18,
                                 1ull << 20};
  std::vector<unsigned> io_depths{1, 4, 8, 16, 32, 64, 128, 512};
  std::vector<unsigned> num_threads{1, 2, 4, 8, 16, 32, 64, 128};
  std::vector<bool> access_patterns{true, false};

  const char *path_to_file = argv[1];

  File file{path_to_file, File::kRead};

  std::cout
      << "access_pattern,num_threads,io_depth_per_thread,page_size,bandwidth\n";

  for (auto access_pattern : access_patterns) {
    for (auto page_size : page_sizes) {
      for (auto io_depth : io_depths) {
        for (auto num_thread : num_threads) {
          Expect(file.file_size % page_size == 0);
          size_t num_pages = file.file_size / page_size;
          std::vector<Entry> raw_entries =
              InitializeEntries(num_pages, page_size, access_pattern);
          std::span<Entry> entries{raw_entries};
          auto num_entries_per_thread =
              (entries.size() + num_thread - 1) / num_thread;

          std::chrono::steady_clock::time_point start_time_point;
          std::chrono::steady_clock::time_point stop_time_point;
          std::barrier barrier(num_thread,
                               [current = &start_time_point,
                                next = &stop_time_point]() mutable noexcept {
                                 *current = std::chrono::steady_clock::now();
                                 current = next;
                               });

          std::vector<std::thread> threads;
          threads.reserve(num_thread);
          std::vector<size_t> num_completed_io_operations_per_thread(num_thread,
                                                                     0ull);

          for (unsigned thread_index = 0; thread_index != num_thread;
               ++thread_index) {
            auto begin_entries_partition =
                std::min(thread_index * num_entries_per_thread, entries.size());
            auto size_entries_partition =
                std::min(num_entries_per_thread,
                         entries.size() - begin_entries_partition);

            Expect(size_entries_partition >= io_depth);

            threads.emplace_back(
                [entries = entries.subspan(begin_entries_partition,
                                           size_entries_partition),
                 &barrier, &file, &start_time_point,
                 &num_completed_io_operations_per_thread, thread_index,
                 io_depth, page_size]() mutable {
                  num_completed_io_operations_per_thread[thread_index] =
                      DoWork(entries, barrier, file, start_time_point, io_depth,
                             page_size);
                });
          }

          for (auto &thread : threads) {
            thread.join();
          }

          PrintResults(num_completed_io_operations_per_thread, start_time_point,
                       stop_time_point, access_pattern, num_thread, io_depth,
                       page_size);
        }
      }
    }
  }
}