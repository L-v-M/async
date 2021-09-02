#ifndef STORAGE_SWIP_H_
#define STORAGE_SWIP_H_

#include <cstdint>

#include "storage/file.h"

namespace storage {

static_assert(sizeof(uintptr_t) == 8);

class Swip {
 public:
  bool IsPageIndex() const noexcept { return data_ >> 63; }

  bool IsPointer() const noexcept { return !IsPageIndex(); }

  void SetPointer(void *ptr) noexcept {
    data_ = reinterpret_cast<uintptr_t>(ptr);
  }

  void SetPageIndex(PageIndex index) noexcept {
    data_ = static_cast<uintptr_t>(index) | (1ull << 63);
  }

  template <typename T>
  T *GetPointer() const noexcept {
    return reinterpret_cast<T *>(data_);
  }

  PageIndex GetPageIndex() const noexcept {
    constexpr std::uint64_t kMask = (1ull << 63) - 1;
    return data_ & kMask;
  }

  static Swip MakePointer(void *ptr) noexcept {
    Swip swip;
    swip.SetPointer(ptr);
    return swip;
  }

  static Swip MakePageIndex(PageIndex index) noexcept {
    Swip swip;
    swip.SetPageIndex(index);
    return swip;
  }

 private:
  uintptr_t data_;
};

}  // namespace storage

#endif  // STORAGE_SWIP_H_