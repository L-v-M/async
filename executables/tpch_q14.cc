#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <latch>
#include <mutex>
#include <numeric>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cppcoro/sync_wait.hpp"
#include "cppcoro/task.hpp"
#include "cppcoro/when_all_ready.hpp"
#include "storage/file.h"
#include "storage/io_uring.h"
#include "storage/schema.h"
#include "storage/swip.h"
#include "storage/types.h"

namespace {
using namespace storage;

constexpr unsigned kFetchIncrement = 8u;

class LineitemHashTable {
 public:
  explicit LineitemHashTable(unsigned thread_count)
      : thread_local_entries_(thread_count) {}

  void InsertLocalEntries(const LineitemPage *begin, const LineitemPage *end,
                          unsigned thread_index) {
    auto &entries = thread_local_entries_[thread_index];
    auto lower_date_boundary = Date::FromString("1995-09-01|", '|').value;
    auto upper_date_boundary = Date::FromString("1995-09-30|", '|').value;

    for (auto iter = begin; iter != end; ++iter) {
      for (std::uint32_t i = 0, num_tuples = iter->num_tuples; i != num_tuples;
           ++i) {
        if (lower_date_boundary <= iter->l_shipdate[i] &&
            iter->l_shipdate[i] <= upper_date_boundary) {
          entries.emplace_back(iter->l_partkey[i]);
        }
      }
    }
  }

  void ResizeHashTable() {
    auto total_size = 0ull;
    for (const auto &entries : thread_local_entries_) {
      total_size += entries.size();
    }
    hash_table_.resize(std::bit_ceil(total_size));
    hash_table_mask_ = hash_table_.size() - 1;
    std::cout << "Lineitem hash table contains " << total_size << " entries\n";
  }

  void MergeLocalEntries(unsigned thread_index) noexcept {
    for (auto &entry : thread_local_entries_[thread_index]) {
      auto bucket_index = entry.partkey.hash() & hash_table_mask_;
      Entry *current = &hash_table_[bucket_index];
      Entry *next = std::atomic_ref{current->next}.load();
      while (true) {
        if (current->partkey == entry.partkey) {
          ++std::atomic_ref{current->count};
          break;
        } else if (next == nullptr || entry.partkey < next->partkey) {
          // entry should be inserted after the current entry
          entry.next = next;
          if (std::atomic_ref{current->next}.compare_exchange_weak(next,
                                                                   &entry)) {
            break;
          }
        } else {           // => entry.partkey >= next->partkey
          current = next;  // we can even skip any elements that might have been
                           // inserted between current and next in the meantime
          next = std::atomic_ref{current->next}.load();
        }
      }
    }
  }

  std::uint32_t LookupCountForPartkey(Integer partkey) const noexcept {
    auto bucket_index = partkey.hash() & hash_table_mask_;
    for (Entry *current = hash_table_[bucket_index].next; current != nullptr;
         current = current->next) {
      if (current->partkey == partkey) {
        return current->count;
      } else if (partkey < current->partkey) {
        break;
      }
    }
    return 0u;
  }

 private:
  struct Entry {
    // partkey(0) is smaller than any partkey actually used
    Entry() noexcept : next(nullptr), partkey(0), count(0) {}

    explicit Entry(Integer partkey) noexcept
        : next(nullptr), partkey(partkey), count(1) {}

    Entry *next;
    const Integer partkey;
    std::uint32_t count;
  };

  std::vector<std::vector<Entry>> thread_local_entries_;
  std::vector<Entry> hash_table_;
  std::uint64_t hash_table_mask_;
};

class PartHashTable {
 public:
  PartHashTable(unsigned thread_count, unsigned total_num_pages)
      : thread_local_entries_(thread_count),
        page_references_(total_num_pages),
        part_pages_buffer_(total_num_pages),
        num_used_buffer_pages_(0),
        num_cached_references_(0) {
    swips_.reserve(total_num_pages);
    for (PageIndex i{0}; i != total_num_pages; ++i) {
      swips_.emplace_back(Swip::MakePageIndex(i));
    }
  }

  void InsertLocalEntries(const PartPage *begin, const PartPage *end,
                          PageIndex begin_page_index, unsigned thread_index,
                          const LineitemHashTable &lineitem_hash_table) {
    auto &entries = thread_local_entries_[thread_index];
    auto current_page_index = begin_page_index;

    for (auto iter = begin; iter != end; ++iter, ++current_page_index) {
      std::uint32_t num_references = 0u;
      for (std::uint32_t i = 0, num_tuples = iter->num_tuples; i != num_tuples;
           ++i) {
        auto partkey = iter->p_partkey[i];
        if (auto count = lineitem_hash_table.LookupCountForPartkey(partkey);
            count > 0) {
          entries.emplace_back(swips_[current_page_index], partkey, i);
          num_references += count;
        }
      }

      page_references_[current_page_index].num_references = num_references;
    }
  }

  void ResizeHashTable() {
    auto total_size = 0ull;
    for (const auto &entries : thread_local_entries_) {
      total_size += entries.size();
    }
    hash_table_.resize(std::bit_ceil(total_size));
    hash_table_mask_ = hash_table_.size() - 1;
    std::cout << "Part hash table contains " << total_size << " entries\n\n";
  }

  void MergeLocalEntries(unsigned thread_index) noexcept {
    for (auto &entry : thread_local_entries_[thread_index]) {
      auto bucket_index = entry.partkey.hash() & hash_table_mask_;
      Entry *head = std::atomic_ref{hash_table_[bucket_index]}.load();
      do {
        entry.next = head;
      } while (!std::atomic_ref{std::atomic_ref{hash_table_[bucket_index]}}
                    .compare_exchange_weak(head, &entry));
    }
  }

  struct LookupResult {
    Swip swip;
    std::uint32_t tuple_offset;
  };

  LookupResult LookupPartkey(Integer partkey) const {
    auto bucket_index = partkey.hash() & hash_table_mask_;
    for (Entry *current = hash_table_[bucket_index]; current != nullptr;
         current = current->next) {
      if (current->partkey == partkey) {
        return {current->swip, current->tuple_offset};
      }
    }
    throw "Unable to find partkey";  // this should never happen
  }

  std::uint64_t GetTotalNumPageReferences() const noexcept {
    std::uint64_t count = 0ull;
    for (const auto &page_reference : page_references_) {
      count += page_reference.num_references;
    }
    return count;
  }

  void CacheAtLeastNumReferences(File &part_data_file,
                                 std::uint64_t num_references_to_be_cached) {
    constexpr std::uint64_t kNumConcurrentTasks = 64ull;
    IOUring ring(kNumConcurrentTasks);
    Countdown countdown(kNumConcurrentTasks);

    std::vector<cppcoro::task<void>> tasks;
    tasks.reserve(kNumConcurrentTasks + 1);

    auto global_begin = num_used_buffer_pages_;

    auto num_swips = swips_.size();
    for (; num_cached_references_ < num_references_to_be_cached &&
           num_used_buffer_pages_ != num_swips;
         ++num_used_buffer_pages_) {
      const auto &page_reference = page_references_[num_used_buffer_pages_];
      assert(swips_[num_used_buffer_pages_].GetPageIndex() ==
             num_used_buffer_pages_);
      num_cached_references_ += page_reference.num_references;
    }

    auto global_end = num_used_buffer_pages_;

    auto num_pages = global_end - global_begin;
    std::uint64_t partition_size =
        (num_pages + kNumConcurrentTasks - 1) / kNumConcurrentTasks;
    for (std::uint64_t i = 0; i != kNumConcurrentTasks; ++i) {
      std::uint64_t begin =
          std::min(global_begin + i * partition_size, global_end);
      auto end = std::min(begin + partition_size, global_end);
      tasks.emplace_back(
          AsyncLoadPages(ring, begin, end, countdown, part_data_file));
    }
    tasks.emplace_back(DrainRing(ring, countdown));
    cppcoro::sync_wait(cppcoro::when_all_ready(std::move(tasks)));
  }

  cppcoro::task<void> AsyncLoadPages(IOUring &ring, std::uint64_t begin,
                                     std::uint64_t end, Countdown &countdown,
                                     File &part_data_file) {
    for (std::uint64_t i = begin; i != end; ++i) {
      auto *page = reinterpret_cast<std::byte *>(&part_pages_buffer_[i]);
      co_await part_data_file.AsyncReadPage(ring, i, page);
      swips_[i].SetPointer(page);
    }
    countdown.Decrement();
  }

  std::uint64_t GetNumAlreadyCachedReferences() const noexcept {
    return num_cached_references_;
  }

 private:
  struct Entry {
    Entry(const Swip &swip, Integer partkey,
          std::uint32_t tuple_offset) noexcept
        : next(nullptr),
          swip(swip),
          partkey(partkey),
          tuple_offset(tuple_offset) {}

    Entry *next;
    const Swip &swip;
    const Integer partkey;
    std::uint32_t tuple_offset;
  };

  struct PageReferences {
    std::uint32_t num_references;
  };

  std::vector<std::vector<Entry>> thread_local_entries_;
  std::vector<Swip> swips_;
  std::vector<Entry *> hash_table_;
  std::vector<PageReferences> page_references_;
  std::uint64_t hash_table_mask_;
  std::vector<PartPage> part_pages_buffer_;
  std::uint64_t num_used_buffer_pages_;
  std::uint64_t num_cached_references_;
};

PartHashTable BuildHashTableForPart(std::span<LineitemPage> lineitem_pages,
                                    const char *path_to_part) {
  unsigned thread_count = std::thread::hardware_concurrency();

  // First, we build a hash table on lineitem after applying the predicate used
  // in query 14. We need this hash table to figure out which partkeys are
  // actually required by the query and how often each page of the part relation
  // is accessed so that we can correctly implement caching for the benchmark
  // later.
  LineitemHashTable lineitem_hash_table{thread_count};
  {
    auto total_num_pages = lineitem_pages.size();
    auto num_pages_per_thread =
        (total_num_pages + thread_count - 1) / thread_count;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    std::once_flag flag;
    std::latch latch{thread_count};

    for (unsigned thread_index = 0; thread_index != thread_count;
         ++thread_index) {
      threads.emplace_back([thread_index, total_num_pages, num_pages_per_thread,
                            lineitem_pages, &flag, &latch,
                            &lineitem_hash_table]() {
        auto begin =
            std::min(thread_index * num_pages_per_thread, total_num_pages);
        auto end = std::min(begin + num_pages_per_thread, total_num_pages);
        lineitem_hash_table.InsertLocalEntries(
            &lineitem_pages[begin], &lineitem_pages[end], thread_index);
        latch.arrive_and_wait();
        std::call_once(flag, [&lineitem_hash_table]() {
          lineitem_hash_table.ResizeHashTable();
        });
        lineitem_hash_table.MergeLocalEntries(thread_index);
      });
    }

    for (auto &t : threads) {
      t.join();
    }
  }

  // Now, we build a hash table for the part relation which contains only the
  // partkeys that are actually required to process query 14. While building
  // the hash table, we also remember how often each page of the part relation
  // will be accessed for processing query 14.
  int fd = open(path_to_part, O_RDONLY);
  std::uint64_t size_in_bytes = lseek(fd, 0, SEEK_END);
  auto *data = reinterpret_cast<PartPage *>(
      mmap(nullptr, size_in_bytes, PROT_READ, MAP_SHARED, fd, 0));
  madvise(data, size_in_bytes, MADV_SEQUENTIAL);
  madvise(data, size_in_bytes, MADV_WILLNEED);

  auto total_num_pages = size_in_bytes / kPageSize;
  auto num_pages_per_thread =
      (total_num_pages + thread_count - 1) / thread_count;
  std::vector<std::thread> threads;
  threads.reserve(thread_count);

  std::once_flag flag;
  std::latch latch{thread_count};

  PartHashTable part_hash_table(thread_count, total_num_pages);

  for (unsigned thread_index = 0; thread_index != thread_count;
       ++thread_index) {
    threads.emplace_back([thread_index, total_num_pages, num_pages_per_thread,
                          data, &part_hash_table, &lineitem_hash_table, &latch,
                          &flag]() {
      auto begin =
          std::min(thread_index * num_pages_per_thread, total_num_pages);
      auto end = std::min(begin + num_pages_per_thread, total_num_pages);
      part_hash_table.InsertLocalEntries(&data[begin], &data[end], begin,
                                         thread_index, lineitem_hash_table);
      latch.arrive_and_wait();
      std::call_once(
          flag, [&part_hash_table]() { part_hash_table.ResizeHashTable(); });
      part_hash_table.MergeLocalEntries(thread_index);
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  return part_hash_table;
}

class QueryRunner {
 public:
  QueryRunner(const PartHashTable &part_hash_table, File &part_data_file,
              std::span<LineitemPage> lineitem_pages, unsigned thread_count,
              std::uint32_t num_ring_entries = 0)
      : part_hash_table_(part_hash_table),
        part_data_file_(part_data_file),
        lineitem_pages_(lineitem_pages),
        thread_count_(thread_count),
        thread_local_sums_(thread_count),
        lower_date_boundary(Date::FromString("1995-09-01|", '|').value),
        upper_date_boundary(Date::FromString("1995-09-30|", '|').value),
        num_ring_entries_(num_ring_entries) {
    if (num_ring_entries_ > 0) {
      thread_local_rings_.reserve(thread_count_);
      for (unsigned i = 0; i != thread_count; ++i) {
        thread_local_rings_.emplace_back(num_ring_entries_);
      }
    }
  }

  void StartProcessing() {
    std::atomic<std::uint64_t> current_lineitem_page_offset{0ull};
    std::vector<std::thread> threads;
    threads.reserve(thread_count_);

    for (unsigned thread_index = 0; thread_index != thread_count_;
         ++thread_index) {
      threads.emplace_back(
          [is_synchronous = IsSynchronous(),
           num_ring_entries = num_ring_entries_, &current_lineitem_page_offset,
           total_num_pages_lineitem = lineitem_pages_.size(),
           lineitem_pages = lineitem_pages_, this, thread_index,
           &ring = thread_local_rings_[thread_index]] {
            std::vector<cppcoro::task<void>> tasks;
            std::vector<PartPage> part_pages_buffer(
                is_synchronous ? 1 : num_ring_entries);

            while (true) {
              auto fetch_increment =
                  is_synchronous ? kFetchIncrement : num_ring_entries;
              auto begin =
                  current_lineitem_page_offset.fetch_add(fetch_increment);
              if (begin >= total_num_pages_lineitem) {
                return;
              }
              auto end =
                  std::min(begin + fetch_increment, total_num_pages_lineitem);

              if (is_synchronous) {
                for (; begin != end; ++begin) {
                  ProcessLineitemPage(lineitem_pages[begin],
                                      part_pages_buffer.front(), thread_index);
                }
              } else {
                const std::uint64_t num_concurrent_tasks = end - begin;
                Countdown countdown(num_concurrent_tasks);
                tasks.resize(num_concurrent_tasks + 1);
                for (std::uint32_t i = 0; begin != end; ++i, ++begin) {
                  tasks[i] = AsyncProcessLineitemPage(
                      lineitem_pages[begin], part_pages_buffer[i], thread_index,
                      ring, countdown);
                }
                tasks[num_concurrent_tasks] = DrainRing(ring, countdown);
                cppcoro::sync_wait(cppcoro::when_all_ready(std::move(tasks)));
              }
            }
          });
    }

    for (auto &t : threads) {
      t.join();
    }
  }

  void DoPostProcessing(bool should_print_result) const {
    Numeric<12, 4> first_sum;
    Numeric<12, 4> second_sum;
    for (const auto &local_sums : thread_local_sums_) {
      first_sum += local_sums.first;
      second_sum += local_sums.second;
    }

    // 100 * first_sum / second_sum
    auto result = Numeric<12, 4>{1'000'000ll} * (first_sum / second_sum);

    if (should_print_result) {
      std::cout << "promo_revenue\n" << result << "\n";
    }
  }

 private:
  void ProcessLineitemPage(const LineitemPage &lineitem_page, PartPage &buffer,
                           unsigned thread_index) {
    Numeric<12, 4> first_sum;
    Numeric<12, 4> second_sum;
    for (std::uint32_t i = 0, num_tuples = lineitem_page.num_tuples;
         i != num_tuples; ++i) {
      if (lower_date_boundary <= lineitem_page.l_shipdate[i] &&
          lineitem_page.l_shipdate[i] <= upper_date_boundary) {
        auto lookup_result =
            part_hash_table_.LookupPartkey(lineitem_page.l_partkey[i]);

        const PartPage *part_page;
        if (lookup_result.swip.IsPageIndex()) {
          part_data_file_.ReadPage(lookup_result.swip.GetPageIndex(),
                                   reinterpret_cast<std::byte *>(&buffer));
          part_page = &buffer;
        } else {
          part_page = lookup_result.swip.GetPointer<const PartPage>();
        }

        auto sum = lineitem_page.l_extendedprice[i] *
                   (Numeric<12, 2>{100ll} - lineitem_page.l_discount[i]);
        std::string_view p_type(
            part_page->p_type[lookup_result.tuple_offset].Begin(),
            part_page->p_type[lookup_result.tuple_offset].Size());
        if (p_type.starts_with("PROMO")) {
          first_sum += sum;
        }
        second_sum += sum;
      }
    }
    thread_local_sums_[thread_index].first += first_sum;
    thread_local_sums_[thread_index].second += second_sum;
  }

  cppcoro::task<void> AsyncProcessLineitemPage(
      const LineitemPage &lineitem_page, PartPage &buffer,
      unsigned thread_index, IOUring &ring, Countdown &countdown) {
    Numeric<12, 4> first_sum;
    Numeric<12, 4> second_sum;
    for (std::uint32_t i = 0, num_tuples = lineitem_page.num_tuples;
         i != num_tuples; ++i) {
      if (lower_date_boundary <= lineitem_page.l_shipdate[i] &&
          lineitem_page.l_shipdate[i] <= upper_date_boundary) {
        auto lookup_result =
            part_hash_table_.LookupPartkey(lineitem_page.l_partkey[i]);

        const PartPage *part_page;
        if (lookup_result.swip.IsPageIndex()) {
          co_await part_data_file_.AsyncReadPage(
              ring, lookup_result.swip.GetPageIndex(),
              reinterpret_cast<std::byte *>(&buffer));
          part_page = &buffer;
        } else {
          part_page = lookup_result.swip.GetPointer<const PartPage>();
        }

        auto sum = lineitem_page.l_extendedprice[i] *
                   (Numeric<12, 2>{100ll} - lineitem_page.l_discount[i]);
        std::string_view p_type(
            part_page->p_type[lookup_result.tuple_offset].Begin(),
            part_page->p_type[lookup_result.tuple_offset].Size());
        if (p_type.starts_with("PROMO")) {
          first_sum += sum;
        }
        second_sum += sum;
      }
    }
    thread_local_sums_[thread_index].first += first_sum;
    thread_local_sums_[thread_index].second += second_sum;
    countdown.Decrement();
  }

  bool IsSynchronous() const noexcept { return num_ring_entries_ == 0; }

  using NumericsPair = std::pair<Numeric<12, 4>, Numeric<12, 4>>;

  const PartHashTable &part_hash_table_;
  File &part_data_file_;
  std::span<LineitemPage> lineitem_pages_;
  const std::uint32_t thread_count_;
  std::vector<NumericsPair> thread_local_sums_;
  const Date lower_date_boundary;
  const Date upper_date_boundary;
  std::vector<IOUring> thread_local_rings_;
  const std::uint32_t num_ring_entries_;
};

std::vector<LineitemPage> LoadLineitemRelation(const char *path_to_lineitem) {
  int fd = open(path_to_lineitem, O_RDONLY);
  std::uint64_t size_in_bytes = lseek(fd, 0, SEEK_END);
  auto *data = reinterpret_cast<LineitemPage *>(
      mmap(nullptr, size_in_bytes, PROT_READ, MAP_SHARED, fd, 0));
  auto total_num_pages = size_in_bytes / kPageSize;
  std::vector<LineitemPage> lineitem_pages(total_num_pages);
  std::memcpy(lineitem_pages.data(), data, size_in_bytes);
  return lineitem_pages;
}
}  // namespace

int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " lineitem.dat part.dat\n";
    return 1;
  }

  const char *path_to_lineitem = argv[1];
  const char *path_to_part = argv[2];

  std::vector<LineitemPage> lineitem_pages =
      LoadLineitemRelation(path_to_lineitem);

  auto part_hash_table = BuildHashTableForPart(lineitem_pages, path_to_part);

  File part_data_file{path_to_part, File::kRead, true};

  auto total_num_references = part_hash_table.GetTotalNumPageReferences();
  auto ten_percent = (total_num_references + 9) / 10;

  for (int i = 0; i != 11; ++i) {
    std::cout << "\nCached "
              << part_hash_table.GetNumAlreadyCachedReferences() * 100 /
                     total_num_references
              << "%\n";
    {
      QueryRunner synchronousRunner{part_hash_table, part_data_file,
                                    lineitem_pages, 8};
      auto start = std::chrono::high_resolution_clock::now();
      synchronousRunner.StartProcessing();
      synchronousRunner.DoPostProcessing(true);
      auto end = std::chrono::high_resolution_clock::now();
      auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "Synchronous, " << milliseconds << " ms\n";
    }

    {
      QueryRunner asynchronousRunner{part_hash_table, part_data_file,
                                     lineitem_pages, 8, 4};
      auto start = std::chrono::high_resolution_clock::now();
      asynchronousRunner.StartProcessing();
      asynchronousRunner.DoPostProcessing(true);
      auto end = std::chrono::high_resolution_clock::now();
      auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "Asynchronous, " << milliseconds << " ms\n";
    }

    part_hash_table.CacheAtLeastNumReferences(part_data_file,
                                              (i + 1) * ten_percent);
  }
}