#include "storage/types.h"

#include <cstdint>

namespace storage {

static ParseResult<uint32_t> ParseNumber(const char* iter,
                                         char delimiter) noexcept {
  uint32_t number = 0;
  for (; *iter != delimiter; ++iter) {
    number = 10 * number + (*iter - '0');
  }
  return {number, iter};
}

// Algorithm from the Calendar FAQ
static uint32_t MergeJulianDay(uint32_t year, uint32_t month,
                               uint32_t day) noexcept {
  uint32_t a = (14 - month) / 12;
  uint32_t y = year + 4800 - a;
  uint32_t m = month + (12 * a) - 3;

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

// Algorithm from the Calendar FAQ
static void SplitJulianDay(unsigned jd, unsigned& year, unsigned& month,
                           unsigned& day) {
  unsigned a = jd + 32044;
  unsigned b = (4 * a + 3) / 146097;
  unsigned c = a - ((146097 * b) / 4);
  unsigned d = (4 * c + 3) / 1461;
  unsigned e = c - ((1461 * d) / 4);
  unsigned m = (5 * e + 2) / 153;

  day = e - ((153 * m + 2) / 5) + 1;
  month = m + 3 - (12 * (m / 10));
  year = (100 * b) + d - 4800 + (m / 10);
}

std::ostream& operator<<(std::ostream& out, const Date& value) {
  unsigned year, month, day;
  SplitJulianDay(value.raw_, year, month, day);

  char buffer[30];
  snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", year, month, day);
  return out << buffer;
}

ParseResult<Integer> Integer::FromString(const char* iter,
                                         char delimiter) noexcept {
  // Check for a sign
  bool is_negative = false;
  if ((*iter) == '-') {
    is_negative = true;
    ++iter;
  } else if ((*iter) == '+') {
    ++iter;
  }
  auto parsed_number = ParseNumber(iter, delimiter);
  return ParseResult<Integer>{is_negative
                                  ? Integer{-int32_t(parsed_number.value)}
                                  : Integer(parsed_number.value),
                              parsed_number.end_it};
}

}  // namespace storage