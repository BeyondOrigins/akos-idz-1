#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct {
    uint64_t state;
} Rng;

static inline void rng_seed(Rng* rng, uint64_t seed) {
    rng->state = seed;
}

static inline uint64_t rng_next(Rng* rng) {
    uint64_t z = (rng->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline int rng_below(Rng* rng, int n) {
    return (int)(rng_next(rng) % (uint64_t)n);
}

#endif
