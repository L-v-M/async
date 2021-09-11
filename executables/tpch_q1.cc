#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <span>
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

constexpr bool kDoWork = true;
constexpr unsigned kFetchIncrement = 4u;

class Cache {
 public:
  Cache(std::span<Swip> swips, File &data_file)
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
      LineitemPage &page = frames_.back();
      co_await data_file_.AsyncReadPage(ring,
                                        swips_[swip_indexes[i]].GetPageIndex(),
                                        reinterpret_cast<std::byte *>(&page));
      swips_[swip_indexes[i]].SetPointer(&page);
    }
    countdown.Decrement();
  }

  std::span<Swip> swips_;
  File &data_file_;
  std::vector<LineitemPage> frames_;
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

class QueryRunner {
 public:
  QueryRunner(std::uint32_t num_threads, std::span<const Swip> swips,
              File &data_file, std::uint32_t num_ring_entries = 0)
      : thread_local_hash_tables_(num_threads),
        thread_local_valid_hash_table_indexes_(num_threads),
        high_date_(Date::FromString("1998-09-02 ", ' ').value),
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

  static void ProcessData(LineitemPage *data, HashTable &hash_table,
                          ValidHashTableIndexes &valid_hash_table_indexes,
                          Date high_date) {
    Numeric<12, 2> one{std::int64_t{100}};  // assigns a raw value
    for (std::uint32_t i = 0; i != data->num_tuples; ++i) {
      if (data->l_shipdate[i] <= high_date) {
        std::uint32_t hash_table_index = data->l_returnflag[i];
        hash_table_index = (hash_table_index << 8) + data->l_linestatus[i];
        auto &entry = hash_table[hash_table_index];
        if (!entry) {
          entry = std::make_unique<HashTableEntry>();
          entry->l_returnflag = data->l_returnflag[i];
          entry->l_linestatus = data->l_linestatus[i];
          entry->count = 0;
          valid_hash_table_indexes.push_back(hash_table_index);
        }

        ++entry->count;
        entry->sum_qty += data->l_quantity[i];
        entry->sum_base_price += data->l_extendedprice[i];
        entry->sum_disc += data->l_discount[i];
        Numeric<12, 4> common_term =
            data->l_extendedprice[i] * (one - data->l_discount[i]);
        entry->sum_disc_price += common_term;
        entry->sum_charge += common_term.CastM2() * (one + data->l_tax[i]);
      }
    }
  }

  static void ProcessPage(LineitemPage &page, Swip swip, HashTable &hash_table,
                          ValidHashTableIndexes &valid_hash_table_indexes,
                          Date high_date, File &data_file) {
    LineitemPage *data;

    if (swip.IsPageIndex()) {
      data_file.ReadPage(swip.GetPageIndex(),
                         reinterpret_cast<std::byte *>(&page));
      data = &page;
    } else {
      data = swip.GetPointer<LineitemPage>();
    }
    if constexpr (kDoWork) {
      ProcessData(data, hash_table, valid_hash_table_indexes, high_date);
    }
  }

  static cppcoro::task<void> AsyncProcessPage(
      LineitemPage &page, Swip swip, HashTable &hash_table,
      ValidHashTableIndexes &valid_hash_table_indexes, Date high_date,
      File &data_file, IOUring &ring, Countdown &countdown) {
    LineitemPage *data;

    if (swip.IsPageIndex()) {
      co_await data_file.AsyncReadPage(ring, swip.GetPageIndex(),
                                       reinterpret_cast<std::byte *>(&page));
      data = &page;
    } else {
      data = swip.GetPointer<LineitemPage>();
    }
    if constexpr (kDoWork) {
      ProcessData(data, hash_table, valid_hash_table_indexes, high_date);
    }
    countdown.Decrement();
    co_return;
  }

  bool IsSynchronous() const noexcept { return num_ring_entries_ == 0; }

  void StartProcessing() {
    std::atomic<std::uint64_t> current_swip{0};
    std::vector<std::thread> threads;

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
            std::vector<LineitemPage> pages(is_synchronous ? 1
                                                           : num_ring_entries);

            while (true) {
              auto fetch_increment =
                  is_synchronous ? kFetchIncrement : num_ring_entries;
              auto start = current_swip.fetch_add(fetch_increment);
              if (start >= num_swips) {
                return;
              }
              auto end = std::min(num_swips, start + fetch_increment);

              if (is_synchronous) {
                for (; start != end; ++start) {
                  ProcessPage(pages[0], swips[start], hash_table,
                              valid_hash_table_indexes, high_date, data_file);
                }
              } else {
                const std::uint64_t num_concurrent_tasks = end - start;
                Countdown countdown(num_concurrent_tasks);
                tasks.resize(num_concurrent_tasks + 1);
                for (std::uint32_t i = 0; start != end; ++i, ++start) {
                  tasks[i] =
                      AsyncProcessPage(pages[i], swips[start], hash_table,
                                       valid_hash_table_indexes, high_date,
                                       data_file, ring, countdown);
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

  void DoPostProcessing(bool should_print_result) {
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
      result_entries.push_back(result_hash_table[valid_hash_table_index].get());
    }
    std::sort(result_entries.begin(), result_entries.end(),
              [](HashTableEntry *lhs, HashTableEntry *rhs) {
                return std::pair(lhs->l_returnflag, lhs->l_linestatus) <
                       std::pair(rhs->l_returnflag, rhs->l_linestatus);
              });

    if (should_print_result) {
      std::cout << "l_returnflag|l_linestatus|sum_qty|sum_base_price|sum_disc_"
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

 private:
  std::vector<HashTable> thread_local_hash_tables_;
  std::vector<ValidHashTableIndexes> thread_local_valid_hash_table_indexes_;
  std::vector<IOUring> thread_local_rings_;
  const Date high_date_;
  const std::uint32_t num_threads_;
  std::span<const Swip> swips_;
  File &data_file_;
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
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " lineitem.dat\n";
    return 1;
  }

  std::string path_to_lineitem{argv[1]};

  File file{path_to_lineitem.c_str(), File::kRead, true};
  auto file_size = file.ReadSize();
  auto swips = GetSwips(file_size);

  Cache cache{swips, file};
  std::vector<std::uint64_t> swip_indexes(swips.size());
  std::iota(swip_indexes.begin(), swip_indexes.end(), 0ull);
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(swip_indexes.begin(), swip_indexes.end(), g);

  auto partition_size =
      (swip_indexes.size() + 4) / 5;  // divide in 5 partitions

  for (int i = 0; i != 6; ++i) {
    if (i > 0) {
      auto offset = std::min((i - 1) * partition_size, swip_indexes.size());
      auto size = std::min(partition_size, swip_indexes.size() - offset);
      cache.Populate(
          std::span<const std::uint64_t>{swip_indexes}.subspan(offset, size));
    }

    {
      QueryRunner synchronousRunner{6, swips, file};
      auto start = std::chrono::high_resolution_clock::now();
      synchronousRunner.StartProcessing();
      synchronousRunner.DoPostProcessing(false);
      auto end = std::chrono::high_resolution_clock::now();
      auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "Synchronous, " << i * 20 << "% cached, " << milliseconds
                << " ms, "
                << (file_size / 1000000000.0) / (milliseconds / 1000.0)
                << " Gb/s"
                << "\n";
    }

    {
      QueryRunner asynchronousRunner{6, swips, file, 4};
      auto start = std::chrono::high_resolution_clock::now();
      asynchronousRunner.StartProcessing();
      asynchronousRunner.DoPostProcessing(false);
      auto end = std::chrono::high_resolution_clock::now();
      auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
              .count();
      std::cout << "Asynchronous, " << i * 20 << "% cached, " << milliseconds
                << " ms, "
                << (file_size / 1000000000.0) / (milliseconds / 1000.0)
                << " Gb/s"
                << "\n";
    }
  }
}