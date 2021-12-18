#pragma once

#ifdef GPU_MINER
constexpr char minerVersion[] = "0.15.1p+GPU";
#else
constexpr char minerVersion[] = "0.15.1p";
#endif
