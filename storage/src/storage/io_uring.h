#ifndef STORAGE_IO_URING_H_
#define STORAGE_IO_URING_H_

#include <array>
#include <cstddef>
#include <system_error>

#include "cppcoro/coroutine.hpp"
#include "cppcoro/task.hpp"
#include "liburing.h"

namespace storage {

class IOUring;

class IOUringAwaiter {
 public:
  IOUringAwaiter(IOUring &ring, void *buffer, size_t num_bytes, off_t offset,
                 int fd) noexcept
      : ring_(ring),
        buffer_(buffer),
        num_bytes_(num_bytes),
        offset_(offset),
        fd_(fd) {}

  bool await_ready() const noexcept { return false; }

  void await_suspend(cppcoro::coroutine_handle<> handle);

  __s32 await_resume() const noexcept { return result_; }

  void SetResult(__s32 result) noexcept { result_ = result; }

  cppcoro::coroutine_handle<> GetHandle() const noexcept { return handle_; }

 private:
  cppcoro::coroutine_handle<> handle_;
  IOUring &ring_;
  void *buffer_;
  const size_t num_bytes_;
  const off_t offset_;
  const int fd_;
  __s32 result_;
};

class IOUring {
 public:
  explicit IOUring(unsigned num_entries) : num_waiting_(0) {
    auto result = io_uring_queue_init(num_entries, &ring_, 0);
    if (result != 0) {
      throw std::system_error{-result, std::generic_category()};
    }
  }

  ~IOUring() { io_uring_queue_exit(&ring_); }

  template <size_t kBatchSize = 8>
  void ProcessBatch() noexcept {
    std::array<io_uring_cqe *, kBatchSize> cqes;
    std::array<cppcoro::coroutine_handle<>, kBatchSize> handles;

    // collect up to kBatchSize handles
    unsigned num_returned =
        io_uring_peek_batch_cqe(&ring_, cqes.data(), kBatchSize);
    for (unsigned i = 0; i != num_returned; ++i) {
      auto *awaiter =
          reinterpret_cast<IOUringAwaiter *>(io_uring_cqe_get_data(cqes[i]));
      awaiter->SetResult(cqes[i]->res);
      io_uring_cqe_seen(&ring_, cqes[i]);
      handles[i] = awaiter->GetHandle();
    }
    num_waiting_ -= num_returned;

    // resume all collected handles
    for (unsigned i = 0; i != num_returned; ++i) {
      handles[i].resume();
    }
  }

  bool Empty() const noexcept { return num_waiting_ == 0; }

 private:
  friend class IOUringAwaiter;

  io_uring ring_;
  unsigned num_waiting_;
};

class SubmissionQueueFullError : public std::exception {
  [[nodiscard]] const char *what() const noexcept override {
    return "Submission queue is full";
  }
};

inline void IOUringAwaiter::await_suspend(cppcoro::coroutine_handle<> handle) {
  handle_ = handle;

  io_uring_sqe *sqe = io_uring_get_sqe(&ring_.ring_);
  if (sqe == nullptr) {
    throw SubmissionQueueFullError{};
  }

  io_uring_prep_read(sqe, fd_, buffer_, num_bytes_, offset_);

  io_uring_sqe_set_data(sqe, this);
  io_uring_submit(&ring_.ring_);
  ++ring_.num_waiting_;
}

class Countdown {
 public:
  explicit Countdown(std::uint64_t counter) noexcept : counter_(counter) {}

  void Decrement() noexcept { --counter_; }

  bool IsZero() const noexcept { return counter_ == 0; }

  void Set(std::uint64_t counter) noexcept { counter_ = counter; }

 private:
  std::uint64_t counter_;
};

inline cppcoro::task<void> DrainRing(IOUring &ring,
                                     const Countdown &countdown) {
  while (!countdown.IsZero()) {
    ring.ProcessBatch();
  }
  co_return;
}

}  // namespace storage

#endif  // STORAGE_IO_URING_H_