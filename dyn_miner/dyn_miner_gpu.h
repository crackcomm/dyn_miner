#pragma once
#include "dyn_stratum.h"

#include <CL/cl.h>
#include <CL/cl_platform.h>

#include <memory>
#include <string>
#include <vector>

struct CDynGPUKernel {
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

    cl_platform_id* platform_id;

    CDynGPUKernel();

    void initOpenCL(int platformID, int computeUnits, const std::vector<std::string>& program);

    // prints GPU info
    void print();
};

struct CDynProgramGPU {
    CDynGPUKernel kernel;

    void startMiner(shared_work_t& shared_work, uint32_t numComputeUnits, uint32_t gpuIndex, shares_t& shares);
    struct free_delete {
        void operator()(uint32_t* bc) { free(bc); }
    };

    using byte_code_ptr = std::unique_ptr<uint32_t, free_delete>;

    struct byte_code_t {
        byte_code_ptr ptr;
        std::size_t size;
    };

    struct byte_code_cache_t {
        char prev_block_hash[32] = {0};
        char merkle_root[32] = {0};
        byte_code_t byte_code;
    };

    std::string program;
    byte_code_cache_t byte_code_cache{};

    // loads byte code to cache
    void load_byte_code(const work_t&);
};
