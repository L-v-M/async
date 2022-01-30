#include "storage/file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace {
[[noreturn]] static void ThrowErrno() {
  throw std::system_error{errno, std::system_category()};
}
}  // namespace

namespace storage {

File::File(const char *filename, Mode mode, bool use_direct_io_for_reading) {
  switch (mode) {
    case kRead: {
      fd_ = open(filename, O_RDONLY | O_NOATIME |
                               (use_direct_io_for_reading ? O_DIRECT : 0));
      break;
    }
    case kWrite: {
      fd_ = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600);
      break;
    }
  }
  if (fd_ < 0) {
    ThrowErrno();
  }
}

File::~File() {
  if (close(fd_) == -1) {
    ThrowErrno();
  }
}

size_t File::ReadSize() const {
  struct stat fileStat;
  if (fstat(fd_, &fileStat) < 0) {
    ThrowErrno();
  }
  return fileStat.st_size;
}

void File::ReadBlock(std::byte *data, size_t offset, size_t size) const {
  size_t total_bytes_read = 0ull;
  while (total_bytes_read < size) {
    ssize_t bytes_read =
        pread(fd_, data + total_bytes_read, size - total_bytes_read,
              offset + total_bytes_read);
    if (bytes_read == 0) {
      // end of file, i.e. size was probably larger than the file
      // size
      return;
    }
    if (bytes_read < 0) {
      ThrowErrno();
    }
    total_bytes_read += bytes_read;
  }
}

cppcoro::task<void> File::AsyncReadBlock(IOUring &ring, std::byte *data,
                                         size_t offset, size_t size) const {
  size_t total_bytes_read = 0ull;
  while (total_bytes_read < size) {
    ssize_t bytes_read = co_await IOUringAwaiter(
        ring, data + total_bytes_read, size - total_bytes_read,
        offset + total_bytes_read, fd_);
    if (bytes_read == 0) {
      // end of file, i.e. size was probably larger than the file
      // size
      co_return;
    }
    if (bytes_read < 0) {
      ThrowErrno();
    }
    total_bytes_read += bytes_read;
  }
}

void File::AppendBlock(const std::byte *data, size_t size) {
  ssize_t bytes_written = write(fd_, data, size);
  if (bytes_written == -1) {
    ThrowErrno();
  } else if (static_cast<size_t>(bytes_written) != size) {
    // Recovering from this situation is difficult because other threads can
    // write simultaneously
    throw std::runtime_error{"Unable to append full block"};
  }
}

}  // namespace storage