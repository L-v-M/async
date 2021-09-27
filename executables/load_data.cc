#include <fcntl.h>
#include <sys/mman.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#include "storage/file.h"
#include "storage/find_pattern.h"
#include "storage/schema.h"
#include "storage/swip.h"
#include "storage/types.h"

namespace {

using namespace storage;

constexpr std::uint64_t kWriteSize = 1ull << 22;
static_assert(kWriteSize >= storage::kPageSize);
constexpr std::uint64_t kWriteNumPages = kWriteSize / storage::kPageSize;

template <typename Page>
static const char *InsertLine(const char *begin, const char *end,
                              std::uint64_t index, Page &page);

template <>
const char *InsertLine<LineitemPageQ1>(const char *begin, const char *end,
                                       std::uint64_t index,
                                       LineitemPageQ1 &page) {
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

template <>
const char *InsertLine<LineitemPageQ14>(const char *begin, const char *end,
                                        std::uint64_t index,
                                        LineitemPageQ14 &page) {
  auto iter = FindPatternSlow<'|'>(begin, end) + 1;
  auto parsed_partkey = storage::Integer::FromString(iter, '|');
  page.l_partkey[index] = parsed_partkey.value;
  iter = FindNthPatternFast<'|'>(parsed_partkey.end_it + 1, end, 3) + 1;
  auto parsed_extendedprice = storage::Numeric<12, 2>::FromString(iter, '|');
  page.l_extendedprice[index] = parsed_extendedprice.value;
  auto parsed_discount =
      storage::Numeric<12, 2>::FromString(parsed_extendedprice.end_it + 1, '|');
  page.l_discount[index] = parsed_discount.value;
  iter = FindNthPatternFast<'|'>(parsed_discount.end_it + 1, end, 3) + 1;
  page.l_shipdate[index] = storage::Date::FromString(iter, '|').value;
  return FindPatternFast<'\n'>(iter, end);
}

template <>
const char *InsertLine<PartPage>(const char *begin, const char *end,
                                 std::uint64_t index, PartPage &page) {
  auto parsed_partkey = storage::Integer::FromString(begin, '|');
  page.p_partkey[index] = parsed_partkey.value;
  auto type_begin =
      FindNthPatternFast<'|'>(parsed_partkey.end_it + 1, end, 3) + 1;
  auto type_end = FindPatternFast<'|'>(type_begin, end);
  new (&page.p_type[index]) typeof(page.p_type[index]){type_begin, type_end};
  return FindPatternFast<'\n'>(type_end + 1, end);
}

template <typename Page>
static void LoadChunk(const char *begin, const char *end,
                      storage::File &data_file) {
  std::vector<Page> data(kWriteNumPages);

  while (begin < end) {
    for (std::uint64_t i = 0; i != kWriteNumPages; ++i) {
      auto &page = data[i];
      std::uint64_t tuple_index = 0;
      for (; tuple_index != Page::kMaxNumTuples && begin < end; ++tuple_index) {
        begin = InsertLine<Page>(begin, end, tuple_index, page) + 1;
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

template <typename Page>
static void LoadFile(const char *path_to_data_in,
                     const char *path_to_data_out) {
  int fd = open(path_to_data_in, O_RDONLY);
  auto length = lseek(fd, 0, SEEK_END);

  void *data = mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
  madvise(data, length, MADV_SEQUENTIAL);
  madvise(data, length, MADV_WILLNEED);

  auto begin = static_cast<const char *>(data);
  auto end = begin + length;

  storage::File output_file{path_to_data_out, storage::File::kWrite};

  auto thread_count = std::thread::hardware_concurrency();
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  auto start_time = std::chrono::high_resolution_clock::now();

  for (unsigned index = 0; index != thread_count; ++index) {
    threads.emplace_back([index, thread_count, begin, end, &output_file]() {
      auto from = FindBeginBoundary<'\n'>(begin, end, thread_count, index);
      auto to = FindBeginBoundary<'\n'>(begin, end, thread_count, index + 1);
      LoadChunk<Page>(from, to, output_file);
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

static void PrintUsage(const char *command) {
  std::cerr
      << "Usage: " << command
      << " lineitemQ1|lineitemQ14|part (lineitem.tbl "
         "lineitemQ1.dat)|(lineitem.tbl lineitemQ14.dat)|(part.tbl part.dat)\n";
}
}  // namespace

int main(int argc, char *argv[]) {
  if (argc != 4) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string_view kind{argv[1]};
  if (kind == "lineitemQ1") {
    LoadFile<LineitemPageQ1>(argv[2], argv[3]);
  } else if (kind == "lineitemQ14") {
    LoadFile<LineitemPageQ14>(argv[2], argv[3]);
  } else if (kind == "part") {
    LoadFile<PartPage>(argv[2], argv[3]);
  } else {
    PrintUsage(argv[0]);
    return 1;
  }
}