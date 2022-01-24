// Code adapted from
// https://github.com/lemire/testingRNG/blob/master/source/lehmer64.h

#ifndef LEHMER64_H
#define LEHMER64_H

#include "splitmix64.h"

using lehmer64_state_t = __uint128_t;

/**
 * D. H. Lehmer, Mathematical methods in large-scale computing units.
 * Proceedings of a Second Symposium on Large Scale Digital Calculating
 * Machinery;
 * Annals of the Computation Laboratory, Harvard Univ. 26 (1951), pp. 141-146.
 *
 * P L'Ecuyer,  Tables of linear congruential generators of different sizes and
 * good lattice structure. Mathematics of Computation of the American
 * Mathematical
 * Society 68.225 (1999): 249-260.
 */

static inline void lehmer64_seed(lehmer64_state_t* state, uint64_t seed) {
  *state = (((__uint128_t)splitmix64_stateless(seed)) << 64) +
           splitmix64_stateless(seed + 1);
}

static inline uint64_t lehmer64(lehmer64_state_t* state) {
  *state *= UINT64_C(0xda942042e4dd58b5);
  return *state >> 64;
}

#endif