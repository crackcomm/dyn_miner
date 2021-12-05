#pragma once

#ifdef GPU_MINER
constexpr char minerVersion[] = "0.15p+GPU";
#else
constexpr char minerVersion[] = "0.15p";
#endif
