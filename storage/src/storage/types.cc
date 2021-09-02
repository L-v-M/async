#include "storage/types.h"

#include <cstdint>

namespace storage {

static ParseResult<std::uint32_t> ParseNumber(const char* iter,
                                              char delimiter) noexcept {
  std::uint32_t number = 0;
  for (; *iter != delimiter; ++iter) {
    number = 10 * number + (*iter - '0');
  }
  return {number, iter};
}

// Algorithm from the Calendar FAQ
static std::uint32_t MergeJulianDay(std::uint32_t year, std::uint32_t month,
                                    std::uint32_t day) noexcept {
  std::uint32_t a = (14 - month) / 12;
  std::uint32_t y = year + 4800 - a;
  std::uint32_t m = month + (12 * a) - 3;

  return day + ((153 * m + 2) / 5) + (365 * y) + (y / 4) - (y / 100) +
         (y / 400) - 32045;
}

ParseResult<Date> Date::FromString(const char* iter, char delimiter) noexcept {
  auto parsed_year = ParseNumber(iter, '-');
  auto parsed_month = ParseNumber(++parsed_year.end_it, '-');
  auto parsed_day = ParseNumber(++parsed_month.end_it, delimiter);
  return {Date{MergeJulianDay(parsed_year.value, parsed_month.value,
                              parsed_day.value)},
          parsed_day.end_it};
}

}  // namespace storage