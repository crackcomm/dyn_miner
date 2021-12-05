#pragma once

#include <cassert>
#include <cstdint>

inline uint64_t share_to_target(double diff) {
    assert(diff > 0.0);
    int i, shift = 29;
    unsigned char targ[32];
    for (i = 0; i < 32; i++)
        targ[i] = 0;
    double ftarg = (double)0x0000ffff / diff;
    while (ftarg < (double)0x00008000) {
        shift--;
        ftarg *= 256.0;
    }
    while (ftarg >= (double)0x00800000) {
        shift++;
        ftarg /= 256.0;
    }
    uint32_t nBits = (int)ftarg + (shift << 24);
    shift = (nBits >> 24) & 0x00ff;
    nBits &= 0x00FFFFFF;
    targ[shift - 1] = nBits >> 16;
    targ[shift - 2] = nBits >> 8;
    targ[shift - 3] = nBits;
    uint64_t starget = *(uint64_t*)&targ[24];
    return (starget);
}
