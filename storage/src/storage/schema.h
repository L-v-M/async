#ifndef STORAGE_SCHEMA_H_
#define STORAGE_SCHEMA_H_

#include <array>
#include <cstdint>

#include "storage/file.h"
#include "storage/types.h"

namespace storage {

// kPageSize  => kMaxNumTuples
// 1ull << 22 => 29127
// 1ull << 21 => 14563
// 1ull << 20 => 7281
// 1ull << 19 => 3640
// 1ull << 18 => 1820
// 1ull << 17 => 910
// 1ull << 16 => 455
struct alignas(kPageSize) LineitemPage {
  static constexpr std::uint64_t kMaxNumTuples = 455;
  std::uint32_t num_tuples;
  std::array<Integer, kMaxNumTuples> l_orderkey;
  std::array<Integer, kMaxNumTuples> l_partkey;
  std::array<Integer, kMaxNumTuples> l_suppkey;
  std::array<Integer, kMaxNumTuples> l_linenumber;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_quantity;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_extendedprice;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_discount;
  std::array<Numeric<12, 2>, kMaxNumTuples> l_tax;
  std::array<Char, kMaxNumTuples> l_returnflag;
  std::array<Char, kMaxNumTuples> l_linestatus;
  std::array<Date, kMaxNumTuples> l_shipdate;
  std::array<Date, kMaxNumTuples> l_commitdate;
  std::array<Date, kMaxNumTuples> l_receiptdate;
  std::array<Varchar<25>, kMaxNumTuples> l_shipinstruct;
  std::array<Varchar<10>, kMaxNumTuples> l_shipmode;
  std::array<Varchar<44>, kMaxNumTuples> l_comment;
};

static_assert(sizeof(LineitemPage) == kPageSize);

struct alignas(kPageSize) PartPage {
  static constexpr std::uint64_t kMaxNumTuples = 385;
  std::uint32_t num_tuples;
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