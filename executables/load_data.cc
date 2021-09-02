#include <fcntl.h>
#include <immintrin.h>
#include <sys/mman.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#include "storage/file.h"
#include "storage/schema.h"
#include "storage/swip.h"
#include "storage/types.h"

namespace {

using namespace storage;

constexpr std::uint64_t kWriteSize = 1ull << 22;
static_assert(kWriteSize >= storage::kPageSize);
constexpr std::uint64_t kWriteNumPages = kWriteSize / storage::kPageSize;

// Returns the position of the pattern character within [iter, end), or end if
// not found
template <char kPattern>
const char *FindPatternFast(const char *iter, const char *end) {
  // Loop over the content in blocks of 32 characters
  auto end32 = end - 32;
  const auto expanded_pattern = _mm256_set1_epi8(kPattern);
  for (; iter < end32; iter += 32) {
    // Check the next 32 characters for the pattern
    auto block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(iter));
    auto matches =
        _mm256_movemask_epi8(_mm256_cmpeq_epi8(block, expanded_pattern));
    if (matches) {
      return iter + __builtin_ctzll(matches);
    }
  }

  // Check the last few characters explicitly
  while ((iter < end) && ((*iter) != kPattern)) {
    ++iter;
  }

  return iter;
}

// Returns the position of the n-th occurence of the pattern character within
// [iter, end), or end if not found
template <char kPattern>
static const char *FindNthPatternFast(const char *iter, const char *end,
                                      unsigned n) {
  // Loop over the content in blocks of 32 characters
  auto end32 = end - 32;
  const auto expanded_pattern = _mm256_set1_epi8(kPattern);
  for (; iter < end32; iter += 32) {
    // Check the next 32 characters for the pattern
    auto block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(iter));
    auto matches =
        _mm256_movemask_epi8(_mm256_cmpeq_epi8(block, expanded_pattern));
    if (matches) {
      unsigned num_hits = __builtin_popcountll(matches);
      if (num_hits >= n) {
        for (; n > 1; n--) {
          matches &= (matches - 1);
        }
        return iter + __builtin_ctzll(matches);
      }
      n -= num_hits;
    }
  }

  // Check the last few characters explicitly
  for (; iter < end; ++iter) {
    if ((*iter) == kPattern && (--n) == 0) {
      return iter;
    }
  }

  return end;
}

static const char *InsertLine(const char *begin, const char *end,
                              std::uint64_t index, Page &page) {
  auto iter = FindNthPatternFast<'|'>(begin, end, 4) + 1;
  auto parsed_quantity = storage::Numeric<12, 2>::FromString(iter, '|');
  page.l_quantity[index] = parsed_quantity.value;
  auto parsed_extendedprice =
      storage::Numeric<12, 2>::FromString(parsed_quantity.end_it + 1, '|');
  page.l_extendedprice[index] = parsed_extendedprice.value;
  auto parsed_discount =
      storage::Numeric<12, 2>::FromString(parsed_extendedprice.end_it + 1, '|');
  page.l_discount[index] = parsed_discount.value;
  auto parsed_tax =
      storage::Numeric<12, 2>::FromString(parsed_discount.end_it + 1, '|');
  page.l_tax[index] = parsed_tax.value;
  iter = parsed_tax.end_it + 1;
  page.l_returnflag[index] = *iter;
  iter += 2;
  page.l_linestatus[index] = *iter;
  iter += 2;
  page.l_shipdate[index] = storage::Date::FromString(iter, '|').value;
  return FindPatternFast<'\n'>(iter, end);
}

static void LoadChunk(const char *begin, const char *end,
                      storage::File &data_file) {
  std::vector<Page> data(kWriteNumPages);

  while (begin < end) {
    for (std::uint64_t i = 0; i != kWriteNumPages; ++i) {
      auto &page = data[i];
      std::uint64_t tuple_index = 0;
      for (; tuple_index != Page::kMaxNumTuples && begin < end; ++tuple_index) {
        begin = InsertLine(begin, end, tuple_index, page) + 1;
      }
      page.num_tuples = tuple_index;
      if (begin >= end) {
        // we have reached the end of our chunk
        // write the remaining pages
        auto num_used_pages = i + 1;
        data_file.AppendPages(reinterpret_cast<const std::byte *>(data.data()),
                              num_used_pages);
        return;
      }
    }
    data_file.AppendPages(reinterpret_cast<const std::byte *>(data.data()),
                          kWriteNumPages);
  }
}

// Returns the beginning position of the index-th chunk when dividing the range
// [begin, end) into chunkCount chunks
static const char *FindBeginBoundary(const char *begin, const char *end,
                                     unsigned chunk_count, unsigned index) {
  if (index == 0) {
    return begin;
  }

  if (index == chunk_count) {
    return end;
  }

  const char *approx_chunk_begin =
      begin + ((end - begin) * index / chunk_count);
  return FindPatternFast<'\n'>(approx_chunk_begin, end) + 1;
}

static void LoadFile(const std::string &path_to_lineitem_in,
                     const std::string &path_to_lineitem_out) {
  int fd = open(path_to_lineitem_in.c_str(), O_RDONLY);
  auto length = lseek(fd, 0, SEEK_END);

  void *data = mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
  madvise(data, length, MADV_SEQUENTIAL);
  madvise(data, length, MADV_WILLNEED);

  auto begin = static_cast<const char *>(data);
  auto end = begin + length;

  storage::File output_file{path_to_lineitem_out.c_str(),
                            storage::File::kWrite};

  auto thread_count = std::thread::hardware_concurrency();
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  auto start_time = std::chrono::high_resolution_clock::now();

  for (unsigned index = 0; index != thread_count; ++index) {
    threads.emplace_back([index, thread_count, begin, end, &output_file]() {
      // Executed on a background thread
      auto from = FindBeginBoundary(begin, end, thread_count, index);
      auto to = FindBeginBoundary(begin, end, thread_count, index + 1);
      LoadChunk(from, to, output_file);
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                          end_time - start_time)
                          .count();
  std::cout << "Processed " << length / 1'000'000.0 << " MB in " << milliseconds
            << " ms: " << (length / 1'000'000'000.0) / (milliseconds / 1000.0)
            << " GB/s\n";

  munmap(data, length);
  close(fd);
}
}  // namespace

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " lineitem.tbl lineitem.dat\n";
    return 1;
  }

  std::string path_to_lineitem_in{argv[1]};
  std::string path_to_lineitem_out{argv[2]};

  LoadFile(path_to_lineitem_in, path_to_lineitem_out);
}