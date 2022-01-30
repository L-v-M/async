#ifndef STORAGE_SCHEMA_H_
#define STORAGE_SCHEMA_H_

#include <array>
#include <cstdint>

#include "storage/file.h"
#include "storage/types.h"

namespace storage {

static_assert(kPageSizePower >= 12 && kPageSizePower <= 22);

constexpr std::array<uint64_t, 11> kLineitemPageQ1MaxNumTuples = {
    107, 215, 430, 862, 1724, 3449, 6898, 13796, 27593, 55188, 110376};

struct alignas(kPageSize) LineitemPageQ1 {
  static constexpr uint64_t kMaxNumTuples =
      kLineitemPageQ1MaxNumTuples[kPageSizePower - 12];
  uint32_t num_tuples;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_quantity;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_extendedprice;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_discount;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_tax;
  std::array<Char, kMaxNumTuples> l_returnflag;
  std::array<Char, kMaxNumTuples> l_linestatus;
  std::array<Date, kMaxNumTuples> l_shipdate;
};

static_assert(sizeof(LineitemPageQ1) == kPageSize);

constexpr std::array<uint64_t, 11> kLineitemPageQ14MaxNumTuples = {
    170, 341, 682, 1365, 2730, 5461, 10922, 21845, 43690, 87381, 174762};

struct alignas(kPageSize) LineitemPageQ14 {
  static constexpr uint64_t kMaxNumTuples =
      kLineitemPageQ14MaxNumTuples[kPageSizePower - 12];
  uint32_t num_tuples;
  std::array<Integer, kMaxNumTuples> l_partkey;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_extendedprice;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_discount;
  std::array<Date, kMaxNumTuples> l_shipdate;
};

static_assert(sizeof(LineitemPageQ14) == kPageSize);

constexpr std::array<uint64_t, 11> kPartPageMaxNumTuples = {
    24, 48, 96, 192, 385, 770, 1541, 3084, 6168, 12336, 24672};

struct alignas(kPageSize) PartPage {
  static constexpr uint64_t kMaxNumTuples =
      kPartPageMaxNumTuples[kPageSizePower - 12];
  uint32_t num_tuples;
  std::array<Integer, kMaxNumTuples> p_partkey;
  std::array<Varchar<55>, kMaxNumTuples> p_name;
  std::array<Varchar<25>, kMaxNumTuples> p_mfgr;
  std::array<Varchar<10>, kMaxNumTuples> p_brand;
  std::array<Varchar<25>, kMaxNumTuples> p_type;
  std::array<Integer, kMaxNumTuples> p_size;
  std::array<Varchar<10>, kMaxNumTuples> p_container;
  std::array<Numeric<12, 2>, kMaxNumTuples> p_retailprice;
  std::array<Varchar<23>, kMaxNumTuples> p_comment;
};

static_assert(sizeof(PartPage) == kPageSize);

}  // namespace storage

#endif  // STORAGE_SCHEMA_H_