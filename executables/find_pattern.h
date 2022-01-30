#ifndef STORAGE_FIND_PATTERN_H_
#define STORAGE_FIND_PATTERN_H_

#include <immintrin.h>

namespace storage {
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

// Returns the position of the pattern character within [iter, end), or end if
// not found
template <char kPattern>
const char *FindPatternSlow(const char *iter, const char *end) {
  while ((iter < end) && ((*iter) != kPattern)) {
    ++iter;
  }

  return iter;
}

// Returns the position of the n-th occurence of the pattern character within
// [iter, end), or end if not found
template <char kPattern>
const char *FindNthPatternFast(const char *iter, const char *end, unsigned n) {
  // Loop over the content in blocks of 32 characters
  auto end32 = end - 32;
  const auto expanded_pattern = _mm256_set1_epi8(kPattern);
  for (; iter < end32; iter += 32) {
    // Check the next 32 characters for the pattern
    auto block = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(iter));
    auto matches =
        _mm256_movemask_epi8(_mm256_cmpeq_epi8(block, expanded_pattern));
    if (matches) {
      unsigned num_hits = __builtin_popcount(matches);
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

// Returns the beginning position of the index-th chunk when dividing the range
// [begin, end) into chunkCount chunks
template <char kPattern>
const char *FindBeginBoundary(const char *begin, const char *end,
                              unsigned chunk_count, unsigned index) {
  if (index == 0) {
    return begin;
  }

  if (index == chunk_count) {
    return end;
  }

  const char *approx_chunk_begin =
      begin + ((end - begin) * index / chunk_count);
  return FindPatternFast<kPattern>(approx_chunk_begin, end) + 1;
}

}  // namespace storage

#endif  // STORAGE_FIND_PATTERN_H_