// Code adapted from
// https://github.com/lemire/testingRNG/blob/master/source/wyhash.h

// adapted to this project by D. Lemire, from
// https://github.com/wangyi-fudan/wyhash/blob/master/wyhash.h This uses mum
// hashing.
#include <stdint.h>

static inline uint64_t wyhash64_stateless(uint64_t *seed) {
  *seed += UINT64_C(0x60bee2bee120fc15);
  __uint128_t tmp;
  tmp = (__uint128_t)*seed * UINT64_C(0xa3b195354a39b70d);
  uint64_t m1 = (tmp >> 64) ^ tmp;
  tmp = (__uint128_t)m1 * UINT64_C(0x1b03738712fad5c9);
  uint64_t m2 = (tmp >> 64) ^ tmp;
  return m2;
}