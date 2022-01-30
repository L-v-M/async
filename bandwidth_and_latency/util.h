#ifndef BANDWIDTH_AND_LATENCY_UTIL_H_
#define BANDWIDTH_AND_LATENCY_UTIL_H_

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <random>
#include <span>
#include <system_error>
#include <vector>

constexpr size_t kSizeOfCacheLine = 64ull;

struct alignas(kSizeOfCacheLine) CacheLine {
  CacheLine *next;
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

constexpr size_t kPageSize4KiB = 1ull << 12;
constexpr size_t kPageSize64KiB = 1ull << 16;
constexpr size_t kPageSize512KiB = 1ull << 19;

struct File {
  File(const char *path_name) {
    int flags = O_RDONLY | O_NOATIME | O_DIRECT;
    fd = open(path_name, flags);
    if (fd == -1) {
      throw std::system_error{errno, std::generic_category()};
    }
    file_size = lseek(fd, 0, SEEK_END);
  }

  ~File() { close(fd); }

  int fd;
  off_t file_size;
};

struct FileOffsetEntry {
  off_t offset;
  FileOffsetEntry *next;
};

inline std::vector<FileOffsetEntry> InitializeEntries(size_t num_pages,
                                                      size_t page_size,
                                                      bool do_random_io) {
  std::vector<FileOffsetEntry> entries(num_pages);
  for (size_t i = 0; i != num_pages; ++i) {
    entries[i].offset = i * page_size;
  }

  if (do_random_io) {
    std::random_device rd;
    std::mt19937 g(rd());
    g.seed(42);

    std::shuffle(entries.begin(), entries.end(), g);
  }

  for (size_t i = 0, end = entries.size() - 1; i != end; ++i) {
    entries[i].next = &entries[i + 1];
  }
  entries.back().next = &entries.front();
  return entries;
}

#endif