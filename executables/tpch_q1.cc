#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <span>
#include <sstream>
#include <thread>
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

bool do_work = true;

class Cache {
 public:
  Cache(std::span<Swip> swips, const File &data_file)
      : swips_(swips), data_file_(data_file) {
    frames_.reserve(swips.size());
  }

  void Populate(std::span<const std::uint64_t> swip_indexes) {
    constexpr std::uint64_t kNumConcurrentTasks = 64ull;
    IOUring ring(kNumConcurrentTasks);
    Countdown countdown(kNumConcurrentTasks);

    std::vector<cppcoro::task<void>> tasks;
    tasks.reserve(kNumConcurrentTasks + 1);

    std::uint64_t partition_size =
        (swip_indexes.size() + kNumConcurrentTasks - 1) / kNumConcurrentTasks;

    for (std::uint64_t i = 0; i != kNumConcurrentTasks; ++i) {
      std::uint64_t begin = std::min(i * partition_size, swip_indexes.size());
      auto end = std::min(begin + partition_size, swip_indexes.size());
      tasks.emplace_back(
          AsyncLoadPages(ring, begin, end, countdown, swip_indexes));
    }
    tasks.emplace_back(DrainRing(ring, countdown));
    cppcoro::sync_wait(cppcoro::when_all_ready(std::move(tasks)));
  }

 private:
  cppcoro::task<void> AsyncLoadPages(
      IOUring &ring, std::uint64_t begin, std::uint64_t end,
      Countdown &countdown, std::span<const std::uint64_t> swip_indexes) {
    for (std::uint64_t i = begin; i != end; ++i) {
      frames_.emplace_back();
      LineitemPageQ1 &page = frames_.back();
      co_await data_file_.AsyncReadPage(ring,
                                        swips_[swip_indexes[i]].GetPageIndex(),
                                        reinterpret_cast<std::byte *>(&page));
      swips_[swip_indexes[i]].SetPointer(&page);
    }
    countdown.Decrement();
  }

  std::span<Swip> swips_;
  const File &data_file_;
  std::vector<LineitemPageQ1> frames_;
};

struct HashTableEntry {
  Numeric<12, 2> sum_qty;
  Numeric<12, 2> sum_base_price;
  Numeric<12, 2> sum_disc;
  Numeric<12, 4> sum_disc_price;
  Numeric<12, 4> sum_charge;
  std::uint32_t count;
  Char l_returnflag;
  Char l_linestatus;
};

using HashTable = std::vector<std::unique_ptr<HashTableEntry>>;
using ValidHashTableIndexes = std::vector<std::uint32_t>;

// implementation idea for query 1 stolen from the MonetDB/X100 paper
class QueryRunner {
 public:
  QueryRunner(std::uint32_t num_threads, std::span<const Swip> swips,
              const File &data_file, std::uint32_t num_ring_entries = 0)
      : thread_local_hash_tables_(num_threads),
        thread_local_valid_hash_table_indexes_(num_threads),
        high_date_(Date::FromString("1998-09-02|", '|').value),
        num_threads_(num_threads),
        swips_(swips),
        data_file_(data_file),
        num_ring_entries_(num_ring_entries) {
    for (auto &hash_table : thread_local_hash_tables_) {
      hash_table.resize(1ull << 16);
    }

    if (num_ring_entries > 0) {
      thread_local_rings_.reserve(num_threads);
      for (std::uint32_t i = 0; i != num_threads; ++i) {
        thread_local_rings_.emplace_back(num_ring_entries);
      }
    }
  }

  static void ProcessTuples(const LineitemPageQ1 &page, HashTable &hash_table,
                            ValidHashTableIndexes &valid_hash_table_indexes,
                            Date high_date) {
    Numeric<12, 2> one{std::int64_t{100}};  // assigns a raw value
    for (std::uint32_t i = 0; i != page.num_tuples; ++i) {
      if (page.l_shipdate[i] <= high_date) {
        std::uint32_t hash_table_index = page.l_returnflag[i];
        hash_table_index = (hash_table_index << 8) + page.l_linestatus[i];
        auto &entry = hash_table[hash_table_index];
        if (!entry) {
          entry = std::make_unique<HashTableEntry>();
          entry->l_returnflag = page.l_returnflag[i];
          entry->l_linestatus = page.l_linestatus[i];
          entry->count = 0;
          valid_hash_table_indexes.push_back(hash_table_index);
        }

        ++entry->count;
        entry->sum_qty += page.l_quantity[i];
        entry->sum_base_price += page.l_extendedprice[i];
        entry->sum_disc += page.l_discount[i];
        Numeric<12, 4> common_term =
            page.l_extendedprice[i] * (one - page.l_discount[i]);
        entry->sum_disc_price += common_term;
        entry->sum_charge += common_term.CastM2() * (one + page.l_tax[i]);
      }
    }
  }

  static void ProcessPage(LineitemPageQ1 &page, Swip swip,
                          HashTable &hash_table,
                          ValidHashTableIndexes &valid_hash_table_indexes,
                          Date high_date, const File &data_file) {
    LineitemPageQ1 *data;

    if (swip.IsPageIndex()) {
      data_file.ReadPage(swip.GetPageIndex(),
                         reinterpret_cast<std::byte *>(&page));
      data = &page;
    } else {
      data = swip.GetPointer<LineitemPageQ1>();
    }
    if (do_work) {
      ProcessTuples(*data, hash_table, valid_hash_table_indexes, high_date);
    }
  }

  static cppcoro::task<void> AsyncProcessPage(
      LineitemPageQ1 &page, Swip swip, HashTable &hash_table,
      ValidHashTableIndexes &valid_hash_table_indexes, Date high_date,
      const File &data_file, IOUring &ring, Countdown &countdown) {
    LineitemPageQ1 *data;

    if (swip.IsPageIndex()) {
      co_await data_file.AsyncReadPage(ring, swip.GetPageIndex(),
                                       reinterpret_cast<std::byte *>(&page));
      data = &page;
    } else {
      data = swip.GetPointer<LineitemPageQ1>();
    }
    if (do_work) {
      ProcessTuples(*data, hash_table, valid_hash_table_indexes, high_date);
    }
    countdown.Decrement();
  }

  bool IsSynchronous() const noexcept { return num_ring_entries_ == 0; }

  void StartProcessing() {
    std::atomic<std::uint64_t> current_swip{0ull};
    std::vector<std::thread> threads;
    threads.reserve(num_threads_);

    for (std::uint32_t thread_index = 0; thread_index != num_threads_;
         ++thread_index) {
      threads.emplace_back(
          [&hash_table = thread_local_hash_tables_[thread_index],
           &valid_hash_table_indexes =
               thread_local_valid_hash_table_indexes_[thread_index],
           high_date = high_date_, &current_swip, num_swips = swips_.size(),
           &swips = swips_, &data_file = data_file_,
           is_synchronous = IsSynchronous(),
           &ring = thread_local_rings_[thread_index],
           num_ring_entries = num_ring_entries_] {
            std::vector<cppcoro::task<void>> tasks;
            std::vector<LineitemPageQ1> pages(
                is_synchronous ? 1 : num_ring_entries);

            constexpr std::uint64_t kSyncFetchIncrement =
                (100'000 + LineitemPageQ1::kMaxNumTuples - 1) /
                LineitemPageQ1::kMaxNumTuples;

            std::uint64_t fetch_increment;

            if (is_synchronous) {
              fetch_increment = kSyncFetchIncrement;
            } else {
              fetch_increment = ((kSyncFetchIncrement + num_ring_entries - 1) /
                                 num_ring_entries) *
                                num_ring_entries;
            }

            while (true) {
              auto begin = current_swip.fetch_add(fetch_increment);
              if (begin >= num_swips) {
                return;
              }
              auto end = std::min(num_swips, begin + fetch_increment);

              if (is_synchronous) {
                for (; begin != end; ++begin) {
                  ProcessPage(pages.front(), swips[begin], hash_table,
                              valid_hash_table_indexes, high_date, data_file);
                }
              } else {
                Countdown countdown(0);
                for (std::uint32_t i = 0; begin != end; ++begin) {
                  tasks.emplace_back(
                      AsyncProcessPage(pages[i++], swips[begin], hash_table,
                                       valid_hash_table_indexes, high_date,
                                       data_file, ring, countdown));
                  if (tasks.size() == num_ring_entries) {
                    countdown.Set(num_ring_entries);
                    tasks.emplace_back(DrainRing(ring, countdown));
                    cppcoro::sync_wait(
                        cppcoro::when_all_ready(std::move(tasks)));
                    i = 0;
                  }
                }
                if (!tasks.empty()) {
                  countdown.Set(tasks.size());
                  tasks.emplace_back(DrainRing(ring, countdown));
                  cppcoro::sync_wait(cppcoro::when_all_ready(std::move(tasks)));
                }
              }
            }
          });
    }

    for (auto &t : threads) {
      t.join();
    }
  }

  void DoPostProcessing(bool should_print_result) {
    if (do_work) {
      auto &result_hash_table = thread_local_hash_tables_.front();
      auto &result_valid_hash_table_indexes =
          thread_local_valid_hash_table_indexes_.front();

      for (std::uint32_t i = 1; i != num_threads_; ++i) {
        auto &local_hash_table = thread_local_hash_tables_[i];
        for (auto valid_hash_table_index :
             thread_local_valid_hash_table_indexes_[i]) {
          auto &local_entry = local_hash_table[valid_hash_table_index];
          auto &result_entry = result_hash_table[valid_hash_table_index];
          if (result_entry) {
            result_entry->sum_qty += local_entry->sum_qty;
            result_entry->sum_base_price += local_entry->sum_base_price;
            result_entry->sum_disc += local_entry->sum_disc;
            result_entry->sum_disc_price += local_entry->sum_disc_price;
            result_entry->sum_charge += local_entry->sum_charge;
            result_entry->count += local_entry->count;
          } else {
            result_entry = std::move(local_entry);
            result_valid_hash_table_indexes.push_back(valid_hash_table_index);
          }
        }
      }

      std::vector<HashTableEntry *> result_entries;
      for (auto valid_hash_table_index : result_valid_hash_table_indexes) {
        result_entries.push_back(
            result_hash_table[valid_hash_table_index].get());
      }
      std::sort(result_entries.begin(), result_entries.end(),
                [](HashTableEntry *lhs, HashTableEntry *rhs) {
                  return std::pair(lhs->l_returnflag, lhs->l_linestatus) <
                         std::pair(rhs->l_returnflag, rhs->l_linestatus);
                });

      if (should_print_result) {
        std::cout
            << "l_returnflag|l_linestatus|sum_qty|sum_base_price|sum_disc_"
               "price|sum_charge|avg_qty|avg_price|avg_disc|count_order\n";
        for (auto *entry : result_entries) {
          std::cout << entry->l_returnflag << "|" << entry->l_linestatus << "|"
                    << entry->sum_qty << "|" << entry->sum_base_price << "|"
                    << entry->sum_disc_price << "|" << entry->sum_charge << "|"
                    << entry->sum_qty / entry->count << "|"
                    << entry->sum_base_price / entry->count << "|"
                    << entry->sum_disc / entry->count << "|" << entry->count
                    << "\n";
        }
      }
    }
  }

 private:
  std::vector<HashTable> thread_local_hash_tables_;
  std::vector<ValidHashTableIndexes> thread_local_valid_hash_table_indexes_;
  std::vector<IOUring> thread_local_rings_;
  const Date high_date_;
  const std::uint32_t num_threads_;
  const std::span<const Swip> swips_;
  const File &data_file_;
  const std::uint32_t num_ring_entries_;
};

std::vector<Swip> GetSwips(std::uint64_t size_of_data_file) {
  auto num_pages = size_of_data_file / kPageSize;
  std::vector<Swip> swips;
  swips.reserve(num_pages);
  for (PageIndex i = 0; i != num_pages; ++i) {
    swips.emplace_back(Swip::MakePageIndex(i));
  }
  return swips;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc != 7) {
    std::cerr << "Usage: " << argv[0]
              << " lineitem.dat num_threads num_entries_per_ring do_work "
                 "do_random_io print_result\n";
    return 1;
  }

  std::string path_to_lineitem{argv[1]};
  unsigned num_threads = std::atoi(argv[2]);
  unsigned num_entries_per_ring = std::atoi(argv[3]);
  std::istringstream(argv[4]) >> std::boolalpha >> do_work;
  bool do_random_io;
  std::istringstream(argv[5]) >> std::boolalpha >> do_random_io;
  bool print_result;
  std::istringstream(argv[6]) >> std::boolalpha >> print_result;

  const File file{path_to_lineitem.c_str(), File::kRead, true};
  auto file_size = file.ReadSize();
  auto swips = GetSwips(file_size);

  std::vector<std::uint64_t> swip_indexes(swips.size());
  {
    std::random_device rd;
    std::mt19937 g(rd());

    if (do_random_io) {
      std::shuffle(swips.begin(), swips.end(), g);
    }

    std::iota(swip_indexes.begin(), swip_indexes.end(), 0ull);
    std::shuffle(swip_indexes.begin(), swip_indexes.end(), g);
  }

  Cache cache{swips, file};

  auto partition_size =
      (swip_indexes.size() + 9) / 10;  // divide in 10 partitions

  std::cout << "kind_of_io,num_threads,percent_cached,num_entries_per_ring,do_"
               "work,do_"
               "random_io,time,throughput\n";

  for (int i = 0; i != 11; ++i) {
    if (i > 0) {
      auto offset = std::min((i - 1) * partition_size, swip_indexes.size());
      auto size = std::min(partition_size, swip_indexes.size() - offset);
      cache.Populate(
          std::span<const std::uint64_t>{swip_indexes}.subspan(offset, size));
    }

    {
      QueryRunner synchronousRunner{num_threads, swips, file};
      auto start = std::chrono::high_resolution_clock::now();
      synchronousRunner.StartProcessing();
      synchronousRunner.DoPostProcessing(print_result);
      auto end = std::chrono::high_resolution_clock::now();
      auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "synchronous," << num_threads << "," << i * 10 << " %,0,"
                << std::boolalpha << do_work << "," << do_random_io << ","
                << milliseconds << " ms,"
                << (file_size / 1000000000.0) / (milliseconds / 1000.0)
                << " Gb/s\n";
    }

    {
      QueryRunner asynchronousRunner{num_threads, swips, file,
                                     num_entries_per_ring};
      auto start = std::chrono::high_resolution_clock::now();
      asynchronousRunner.StartProcessing();
      asynchronousRunner.DoPostProcessing(print_result);
      auto end = std::chrono::high_resolution_clock::now();
      auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "asynchronous," << num_threads << "," << i * 10 << " %,"
                << num_entries_per_ring << "," << std::boolalpha << do_work
                << "," << do_random_io << "," << milliseconds << " ms,"
                << (file_size / 1000000000.0) / (milliseconds / 1000.0)
                << " Gb/s\n";
    }
  }
}