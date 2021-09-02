#ifndef STORAGE_SCHEMA_H_
#define STORAGE_SCHEMA_H_

#include <array>
#include <cstdint>

#include "storage/file.h"
#include "storage/types.h"

namespace storage {

// kPageSize  => kMaxNumTuples
// 1ull << 22 => 110376
// 1ull << 21 => 55188
// 1ull << 20 => 27593
// 1ull << 19 => 13796
// 1ull << 18 => 6898
// 1ull << 17 => 3449
// 1ull << 16 => 1724
// 1ull << 14 => 430
// 1ull << 12 => 107
struct alignas(kPageSize) LineitemPage {
  static constexpr std::uint64_t kMaxNumTuples = 1724;
  std::uint32_t num_tuples;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_quantity;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_extendedprice;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_discount;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_tax;
  std::array<Char, kMaxNumTuples> l_returnflag;
  std::array<Char, kMaxNumTuples> l_linestatus;
  std::array<Date, kMaxNumTuples> l_shipdate;
};

static_assert(sizeof(LineitemPage) == kPageSize);

// kPageSize  => kMaxNumTuples
// 1ull << 22 => 29127
// 1ull << 21 => 14563
// 1ull << 20 => 7281
// 1ull << 19 => 3640
// 1ull << 18 => 1820
// 1ull << 17 => 910
// 1ull << 16 => 455
// struct alignas(kPageSize) LineitemPageLarge {
//   static constexpr std::uint64_t kMaxNumTuples = 455;
//   std::uint32_t num_tuples;
//   std::array<Integer, kMaxNumTuples> l_orderkey;
//   std::array<Integer, kMaxNumTuples> l_partkey;
//   std::array<Integer, kMaxNumTuples> l_suppkey;
//   std::array<Integer, kMaxNumTuples> l_linenumber;
//   std::array<Numeric<12, 2>, kMaxNumTuples> l_quantity;
//   std::array<Numeric<12, 2>, kMaxNumTuples> l_extendedprice;
//   std::array<Numeric<12, 2>, kMaxNumTuples> l_discount;
//   std::array<Numeric<12, 2>, kMaxNumTuples> l_tax;
//   std::array<Char, kMaxNumTuples> l_returnflag;
//   std::array<Char, kMaxNumTuples> l_linestatus;
//   std::array<Date, kMaxNumTuples> l_shipdate;
//   std::array<Date, kMaxNumTuples> l_commitdate;
//   std::array<Date, kMaxNumTuples> l_receiptdate;
//   std::array<FixedChar<25>, kMaxNumTuples> l_shipinstruct;
//   std::array<FixedChar<10>, kMaxNumTuples> l_shipmode;
//   std::array<Varchar<44>, kMaxNumTuples> l_comment;
// };

// static_assert(sizeof(LineitemPageLarge) == kPageSize);

using Page = LineitemPage;

}  // namespace storage

#endif  // STORAGE_SCHEMA_H_