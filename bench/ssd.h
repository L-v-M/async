#pragma once

#include <fcntl.h>
#include <liburing.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace ssd {

constexpr size_t kPageSize4KiB = 1ull << 12;
constexpr size_t kPageSize64KiB = 1ull << 16;
constexpr size_t kPageSize512KiB = 1ull << 19;

struct File {
  enum Mode { kRead, kWrite };

  File(const char *path_name, Mode mode) : mode(mode) {
    int flags;
    if (mode == kRead) {
      flags = O_RDONLY | O_NOATIME;
    } else {
      flags = O_WRONLY | O_DSYNC;
    }
    fd = open(path_name, flags | O_DIRECT);
    if (fd == -1) {
      throw std::system_error{errno, std::generic_category()};
    }
    file_size = lseek(fd, 0, SEEK_END);
  }

  ~File() { close(fd); }

  Mode mode;
  int fd;
  off_t file_size;
};

inline void Expect(bool predicate, const char *what = "Expect failed") {
  if (!predicate) {
    throw std::runtime_error{what};
  }
}

struct Entry {
  off_t offset;
  Entry *next;
};

inline std::vector<Entry> InitializeEntries(size_t num_pages, size_t page_size,
                                            bool do_random_io) {
  std::vector<Entry> entries(num_pages);
  for (size_t i = 0; i != num_pages; ++i) {
    entries[i].offset = i * page_size;
  }
  if (do_random_io) {
    std::random_device rd;
    std::mt19937 g(rd());
    g.seed(42);

    std::shuffle(entries.begin(), entries.end(), g);
  }
  return entries;
}

inline void ConnectEntries(std::span<Entry> entries) {
  if (entries.empty()) {
    return;
  }

  for (size_t i = 0, end = entries.size() - 1; i != end; ++i) {
    entries[i].next = &entries[i + 1];
  }
  entries.back().next = &entries.front();
}

inline std::vector<std::unique_ptr<std::byte>> AllocateAlignedBuffers(
    size_t num_buffers, size_t alignment, size_t size) {
  std::vector<std::unique_ptr<std::byte>> buffers(num_buffers);
  for (auto &buffer : buffers) {
    buffer = std::unique_ptr<std::byte>{
        static_cast<std::byte *>(std::aligned_alloc(alignment, size))};
  }
  return buffers;
}

class IOUring {
 public:
  explicit IOUring(unsigned num_entries) {
    auto result = io_uring_queue_init(num_entries, &ring_, 0u);
    if (result != 0) {
      throw std::system_error{-result, std::generic_category()};
    }
  }

  ~IOUring() {
    Wait();
    io_uring_queue_exit(&ring_);
  }

  void Wait() {
    // Wait for the completion of all outstanding I/O requests
    for (; num_waiting_ != 0; --num_waiting_) {
      WaitOne();
    }
  }

  // Submit one request for every buffer in buffers
  const Entry *SubmitRequests(const File &file, std::span<std::byte *> buffers,
                              const Entry *entry, size_t num_bytes) {
    if (file.mode == File::kRead) {
      return SubmitRequests(io_uring_prep_read, file.fd, buffers, entry,
                            num_bytes);
    } else {
      return SubmitRequests(io_uring_prep_write, file.fd, buffers, entry,
                            num_bytes);
    }
  }

  size_t DoBenchmark(const File &file, std::span<std::byte *> buffers,
                     const Entry *entry, size_t num_bytes,
                     std::chrono::steady_clock::time_point stop_time_point) {
    if (file.mode == File::kRead) {
      return DoBenchmark(io_uring_prep_read, file.fd, buffers, entry, num_bytes,
                         stop_time_point);
    } else {
      return DoBenchmark(io_uring_prep_write, file.fd, buffers, entry,
                         num_bytes, stop_time_point);
    }
  }

  class SubmissionQueueFullError : public std::exception {
    [[nodiscard]] const char *what() const noexcept override {
      return "Submission queue is full";
    }
  };

 private:
  template <typename Operation>
  void SubmitOne(Operation op, int fd, std::byte *buffer, const Entry *entry,
                 size_t num_bytes) {
    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (sqe == nullptr) {
      throw SubmissionQueueFullError{};
    }
    op(sqe, fd, buffer, num_bytes, entry->offset);
    io_uring_sqe_set_data(sqe, buffer);
    Expect(io_uring_submit(&ring_) == 1);
  }

  std::byte *WaitOne() {
    io_uring_cqe *cqe;
    int result = io_uring_wait_cqe(&ring_, &cqe);
    if (result != 0) {
      throw std::system_error{-result, std::generic_category()};
    }
    Expect(cqe->res != -1);
    std::byte *buffer = static_cast<std::byte *>(io_uring_cqe_get_data(cqe));
    io_uring_cqe_seen(&ring_, cqe);
    return buffer;
  }

  template <typename Operation>
  const Entry *SubmitRequests(Operation op, int fd,
                              std::span<std::byte *> buffers,
                              const Entry *entry, size_t num_bytes) {
    // Submit one request for every buffer in buffers
    for (std::byte *buffer : buffers) {
      SubmitOne(op, fd, buffer, entry, num_bytes);
      entry = entry->next;
    }
    num_waiting_ += buffers.size();
    return entry;
  }

  template <typename Operation>
  size_t DoBenchmark(Operation op, int fd, std::span<std::byte *> buffers,
                     const Entry *entry, size_t num_bytes,
                     std::chrono::steady_clock::time_point stop_time_point) {
    entry = SubmitRequests(op, fd, buffers, entry, num_bytes);

    size_t num_completed_io_operations = 0ull;
    // Keep the I/O depth at buffers.size()
    for (;; entry = entry->next) {
      auto *buffer = WaitOne();
      ++num_completed_io_operations;

      // Check if we have to stop the benchmark every 8th time to reduce the
      // overhead of checking the timestamp
      if (num_completed_io_operations % 8 == 0 &&
          std::chrono::steady_clock::now() >= stop_time_point) {
        --num_waiting_;
        return num_completed_io_operations;
      }

      SubmitOne(op, fd, buffer, entry, num_bytes);
    }
  }

  io_uring ring_;
  size_t num_waiting_{0};
};
}  // namespace ssd