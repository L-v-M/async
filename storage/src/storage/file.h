#ifndef STORAGE_FILE_H_
#define STORAGE_FILE_H_

#include <cstdint>
#include <span>

#include "cppcoro/task.hpp"
#include "storage/io_uring.h"

namespace storage {

constexpr std::uint64_t kPageSizePower = ASYNCHRONOUS_IO_PAGE_SIZE_POWER;
constexpr std::uint64_t kPageSize = 1ull << kPageSizePower;

using PageIndex = std::uint64_t;

class File {
 public:
  enum Mode { kRead, kWrite };

  // Opens the file
  File(const char *filename, Mode mode, bool use_direct_io_for_reading = false);

  ~File();

  std::uint64_t ReadSize() const;

  void ReadPage(PageIndex page_index, std::byte *data) const {
    auto offset = page_index * kPageSize;
    ReadBlock(data, offset, kPageSize);
  }

  void ReadBlock(std::byte *data, std::uint64_t offset,
                 std::uint64_t size) const;

  cppcoro::task<void> AsyncReadPage(IOUring &ring, PageIndex page_index,
                                    std::byte *data) const {
    auto offset = page_index * kPageSize;
    co_return co_await AsyncReadBlock(ring, data, offset, kPageSize);
  }

  cppcoro::task<void> AsyncReadBlock(IOUring &ring, std::byte *data,
                                     std::uint64_t offset,
                                     std::uint64_t size) const;

  void AppendPages(const std::byte *data, std::uint64_t num_pages) {
    AppendBlock(data, kPageSize * num_pages);
  }

  void AppendBlock(const std::byte *data, std::uint64_t size);

 private:
  int fd_;
};

}  // namespace storage

#endif  // STORAGE_FILE_H_