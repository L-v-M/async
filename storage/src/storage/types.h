#ifndef STORAGE_TYPES_H_
#define STORAGE_TYPES_H_

#include <cstdint>
#include <cstring>
#include <ostream>

namespace storage {

template <typename T>
struct ParseResult {
  T value;
  const char* end_it;
};

using Char = char;

class Date {
 public:
  Date() noexcept = default;

  explicit Date(uint32_t raw) noexcept : raw_(raw) {}

  static ParseResult<Date> FromString(const char* iter,
                                      char delimiter) noexcept;

  bool operator<=(Date d) const noexcept { return raw_ <= d.raw_; }

  friend std::ostream& operator<<(std::ostream& out, const Date& value);

 private:
  uint32_t raw_{0};
};

template <unsigned kLen, unsigned kPrecision>
class Numeric {
 public:
  Numeric() noexcept : raw_(0) {}

  explicit Numeric(int64_t raw) noexcept : raw_(raw) {}

  static ParseResult<Numeric> FromString(const char* iter,
                                         char delimiter) noexcept {
    // Check for a sign
    bool negated = false;
    if ((*iter) == '-') {
      negated = true;
      ++iter;
    } else if ((*iter) == '+') {
      ++iter;
    }

    int64_t result = 0;
    bool fraction = false;
    uint32_t digits_seen_fraction = 0;
    for (; *iter != delimiter; ++iter) {
      char c = *iter;
      if (c == '.') {
        fraction = true;
      } else {
        result = (result * 10) + (c - '0');
        if (fraction) {
          ++digits_seen_fraction;
        }
      }
    }

    static_assert(kPrecision <= 2,
                  "Higher precision not supported for parsing");
    constexpr int64_t shifts[] = {100ll, 10ll, 1ll};
    result *= shifts[digits_seen_fraction];

    if (negated) {
      return {Numeric<kLen, kPrecision>{-result}, iter};
    } else {
      return {Numeric<kLen, kPrecision>{result}, iter};
    }
  }

  Numeric& operator+=(Numeric<kLen, kPrecision> n) noexcept {
    raw_ += n.raw_;
    return *this;
  }

  Numeric operator+(Numeric<kLen, kPrecision> n) const noexcept {
    Numeric r;
    r.raw_ = raw_ + n.raw_;
    return r;
  }

  Numeric operator-(Numeric<kLen, kPrecision> n) const noexcept {
    Numeric r;
    r.raw_ = raw_ - n.raw_;
    return r;
  }

  Numeric operator/(uint32_t n) const noexcept {
    Numeric r;
    r.raw_ = raw_ / n;
    return r;
  }

  Numeric<kLen, kPrecision + kPrecision> operator*(
      Numeric<kLen, kPrecision> n) const noexcept {
    Numeric<kLen, kPrecision + kPrecision> r;
    r.raw_ = raw_ * n.raw_;
    return r;
  }

  template <unsigned l>
  Numeric<kLen, kPrecision> operator/(Numeric<l, 4> n) const noexcept {
    Numeric r;
    r.raw_ = raw_ * 10000 / n.raw_;
    return r;
  }

  Numeric<kLen, kPrecision - 2> CastM2() const noexcept {
    Numeric<kLen, kPrecision - 2> r;
    r.raw_ = raw_ / 100;
    return r;
  }

  int64_t GetRaw() const noexcept { return raw_; }

 private:
  template <unsigned l, unsigned p>
  friend class Numeric;

  int64_t raw_;
};

class Integer {
 public:
  Integer() noexcept : value_(0) {}

  explicit Integer(int32_t value) noexcept : value_(value) {}

  static ParseResult<Integer> FromString(const char* iter,
                                         char delimiter) noexcept;

  uint64_t hash() const {
    uint64_t r = 88172645463325252ull ^ value_;
    r ^= (r << 13);
    r ^= (r >> 7);
    return (r ^ (r << 17));
  }

  bool operator==(Integer other) const noexcept {
    return value_ == other.value_;
  }

  bool operator<(Integer other) const noexcept { return value_ < other.value_; }

 private:
  int32_t value_;
};

template <unsigned kSize>
struct LengthSwitch {};

template <>
struct LengthSwitch<1> {
  using Type = uint8_t;
};

template <>
struct LengthSwitch<2> {
  using Type = uint16_t;
};

template <>
struct LengthSwitch<4> {
  using Type = uint32_t;
};

template <unsigned kMaxLen>
struct LengthIndicator {
  using Type = typename LengthSwitch<((kMaxLen < 256)     ? 1
                                      : (kMaxLen < 65536) ? 2
                                                          : 4)>::Type;
};

/// A variable length string
template <unsigned kMaxLen>
class Varchar {
 public:
  Varchar() noexcept = default;

  Varchar(const char* begin, const char* end) noexcept : size_(end - begin) {
    std::memcpy(data_, begin, size_);
  }

  const char* Begin() const noexcept { return data_; }

  typename LengthIndicator<kMaxLen>::Type Size() const noexcept {
    return size_;
  }

 private:
  typename LengthIndicator<kMaxLen>::Type size_;
  char data_[kMaxLen];
};

}  // namespace storage

template <unsigned kLen, unsigned kPrecision>
std::ostream& operator<<(std::ostream& out,
                         storage::Numeric<kLen, kPrecision> n) {
  int64_t raw = n.GetRaw();
  if (raw < 0) {
    out << '-';
    raw = -raw;
  }
  if (kPrecision == 0) {
    out << raw;
  } else {
    int64_t sep = 10;
    for (unsigned index = 1; index < kPrecision; ++index) {
      sep *= 10;
    }
    out << (raw / sep);
    out << '.';
    raw = raw % sep;
    if (!raw) {
      for (unsigned index = 0; index < kPrecision; ++index) {
        out << '0';
      }
    } else {
      while (sep > (10 * raw)) {
        out << '0';
        sep /= 10;
      }
      out << raw;
    }
  }
  return out;
}

#endif  // STORAGE_TYPES_H_