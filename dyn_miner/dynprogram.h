#pragma once
#include "common.h"
#include "dyn_miner.h"
#include "sha256.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifdef GPU_MINER
#include <CL/cl.h>
#include <CL/cl_platform.h>
#endif

// WHISKERZ CODE

constexpr auto BLACK = 0;
constexpr auto BLUE = 1;
constexpr auto GREEN = 2;
constexpr auto CYAN = 3;
constexpr auto RED = 4;
constexpr auto MAGENTA = 5;
constexpr auto BROWN = 6;
constexpr auto LIGHTGRAY = 7;
constexpr auto DARKGRAY = 8;
constexpr auto LIGHTBLUE = 9;
constexpr auto LIGHTGREEN = 10;
constexpr auto LIGHTCYAN = 11;
constexpr auto LIGHTRED = 12;
constexpr auto LIGHTMAGENTA = 13;
constexpr auto YELLOW = 14;
constexpr auto WHITE = 15;

// For displaying Hashrate in easy-to-read format.
constexpr float tb = 1099511627776;
constexpr float gb = 1073741824;
constexpr float mb = 1048576;
constexpr float kb = 1024;

constexpr char minerVersion[] = "0.14w";
// WHISKERZ

#define HASHOP_ADD 0
#define HASHOP_XOR 1
#define HASHOP_SHA_SINGLE 2
#define HASHOP_SHA_LOOP 3
#define HASHOP_MEMGEN 4
#define HASHOP_MEMADD 5
#define HASHOP_MEMXOR 6
#define HASHOP_MEM_SELECT 7
#define HASHOP_END 8

class CDynProgram {
public:
    int32_t startingTime;
    std::vector<std::string> program;

#ifdef GPU_MINER
    uint32_t numOpenCLDevices;
    cl_device_id* openCLDevices;

    cl_mem* clGPUProgramBuffer;

    uint32_t hashResultSize;
    cl_mem* clGPUHashResultBuffer;
    uint32_t** buffHashResult;

    uint32_t headerBuffSize;
    cl_mem* clGPUHeaderBuffer;
    unsigned char** buffHeader;

    cl_kernel* kernel;
    cl_command_queue* command_queue;

    void initOpenCL(int platformID, int computeUnits);
    int executeGPU(
      unsigned char* blockHeader,
      std::string prevBlockHash,
      std::string merkleRoot,
      unsigned char* nativeTarget,
      uint32_t* resultNonce,
      int numComputeUnits,
      uint32_t serverNonce,
      int gpuIndex,
      CDynProgram* dynProgram); // WHISKERZ
    uint32_t* executeGPUAssembleByteCode(
      uint32_t* largestMemgen, std::string prevBlockHash, std::string merkleRoot, uint32_t* byteCodeLen);
#endif

    const std::vector<char> hexDigit = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    std::string execute(unsigned char* blockHeader, std::string prevBlockHash, std::string merkleRoot);

    std::string getProgramString();
    void parseHex(std::string input, unsigned char* output);
    unsigned char decodeHex(char in);
    std::string makeHex(unsigned char* in, int len);

    // WHISKERZ CODE
    // bool checkBlockHeight(CDynProgram*);
#ifdef _WIN32
    bool outputStats(CDynProgram*, time_t, time_t, uint32_t);
#endif
    time_t miningStartTime;
    bool checkingHeight = false;
    bool timeout = false;

    int acceptedBlocks;
    int rejectedBlocks;
    int height;

    char* strRPC_URL;
    char* RPCUser;
    char* RPCPassword;
    std::string minerPayToAddr;
    char* minerType;
    // WHISKERZ
};
