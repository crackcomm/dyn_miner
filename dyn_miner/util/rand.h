#pragma once

#include "dyn_stratum.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>

inline uint32_t rand_nonce() {
    time_t t;
    time(&t);
    srand(t);

#ifdef _WIN32
    uint32_t nonce = rand() * t * GetTickCount();
#endif

#ifdef __linux__
    uint32_t nonce = rand() * t;
#endif
    return nonce;
}