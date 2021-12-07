#include "dyn_miner_gpu.h"

#include "dyn_ops.h"
#include "dyn_stratum.h"
#include "util/hex.h"
#include "util/rand.h"
#include "util/stats.h"

#include <iterator>
#include <sstream>

static std::vector<uint32_t> executeAssembleByteCode(
  uint32_t* largestMemgen, const std::vector<std::string>& program, const char* prevBlockHash, const char* merkleRoot) {
    std::vector<uint32_t> code;

    int line_ptr = 0;             // program execution line pointer
    int loop_counter = 0;         // counter for loop execution
    unsigned int memory_size = 0; // size of current memory pool
    uint32_t* memPool = NULL;     // memory pool

    while (line_ptr < program.size()) {
        std::istringstream iss(program[line_ptr]);
        std::vector<std::string> tokens{
          std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{}}; // split line into tokens

        // simple ADD and XOR functions with one constant argument
        if (tokens[0] == "ADD") {
            uint32_t arg1[8];
            parseHex(tokens[1], (unsigned char*)arg1);
            code.push_back(HASHOP_ADD);
            for (int i = 0; i < 8; i++)
                code.push_back(arg1[i]);
        }

        else if (tokens[0] == "XOR") {
            uint32_t arg1[8];
            code.push_back(HASHOP_XOR);
            parseHex(tokens[1], (unsigned char*)arg1);
            for (int i = 0; i < 8; i++)
                code.push_back(arg1[i]);
        }

        // hash algo which can be optionally repeated several times
        else if (tokens[0] == "SHA2") {
            if (tokens.size() == 2) { // includes a loop count
                loop_counter = atoi(tokens[1].c_str());
                code.push_back(HASHOP_SHA_LOOP);
                code.push_back(loop_counter);
            }

            else { // just a single run
                if (tokens[0] == "SHA2") {
                    code.push_back(HASHOP_SHA_SINGLE);
                }
            }
        }

        // generate a block of memory based on a hashing algo
        else if (tokens[0] == "MEMGEN") {
            memory_size = atoi(tokens[2].c_str());
            code.push_back(HASHOP_MEMGEN);
            code.push_back(memory_size);
            if (memory_size > *largestMemgen) *largestMemgen = memory_size;
        }

        // add a constant to every value in the memory block
        else if (tokens[0] == "MEMADD") {
            code.push_back(HASHOP_MEMADD);
            uint32_t arg1[8];
            parseHex(tokens[1], (unsigned char*)arg1);
            for (int j = 0; j < 8; j++)
                code.push_back(arg1[j]);
        }

        // xor a constant with every value in the memory block
        else if (tokens[0] == "MEMXOR") {
            code.push_back(HASHOP_MEMXOR);
            uint32_t arg1[8];
            parseHex(tokens[1], (unsigned char*)arg1);
            for (int j = 0; j < 8; j++)
                code.push_back(arg1[j]);
        }

        // read a value based on an index into the generated block of memory
        else if (tokens[0] == "READMEM") {
            code.push_back(HASHOP_MEM_SELECT);
            if (tokens[1] == "MERKLE") {
                uint32_t v0 = *(uint32_t*)merkleRoot;
                code.push_back(v0);
            }

            else if (tokens[1] == "HASHPREV") {
                uint32_t v0 = *(uint32_t*)prevBlockHash;
                code.push_back(v0);
            }
        }

        line_ptr++;
    }

    code.push_back(HASHOP_END);

    return code;
}

inline CDynProgramGPU::byte_code_t vector_to_contiguous(std::vector<uint32_t> code) {
    CDynProgramGPU::byte_code_ptr result((uint32_t*)malloc(sizeof(uint32_t) * code.size()));
    for (int i = 0; i < code.size(); i++)
        result.get()[i] = code.at(i);
    return {.ptr = std::move(result), .size = code.size() * 4};
}

void CDynProgramGPU::load_byte_code(const work_t& work) {
    if (strcmp(work.prev_block_hash, prev_block_hash) == 0 && strcmp(work.merkle_root, merkle_root) == 0) {
        return;
    }
    // TODO: buffer resizing
    uint32_t largestMemgen{};
    std::vector<uint32_t> byteCode = executeAssembleByteCode(
      &largestMemgen, work.program, work.prev_block_hash, work.merkle_root); // only calling to get largestMemgen
    byte_code = vector_to_contiguous(byteCode);
    memcpy(prev_block_hash, work.prev_block_hash, 32);
    memcpy(merkle_root, work.merkle_root, 32);
}

CDynGPUKernel::CDynGPUKernel() {
    openCLDevices = (cl_device_id*)malloc(16 * sizeof(cl_device_id));

    kernel = (cl_kernel*)malloc(16 * sizeof(cl_kernel));
    command_queue = (cl_command_queue*)malloc(16 * sizeof(cl_command_queue));

    clGPUHashResultBuffer = (cl_mem*)malloc(16 * sizeof(cl_mem));
    buffHashResult = (uint32_t**)malloc(16 * sizeof(uint32_t*));

    clGPUHeaderBuffer = (cl_mem*)malloc(16 * sizeof(cl_mem));
    buffHeader = (unsigned char**)malloc(16 * sizeof(char*));
    clGPUProgramBuffer = (cl_mem*)malloc(16 * sizeof(cl_mem));
    platform_id = (cl_platform_id*)malloc(16 * sizeof(cl_platform_id));
}

void CDynGPUKernel::print() {
    cl_int returnVal;
    cl_device_id* device_id = (cl_device_id*)malloc(16 * sizeof(cl_device_id));
    cl_uint ret_num_platforms;

    cl_ulong globalMem;
    cl_uint computeUnits;
    size_t sizeRet;

    returnVal = clGetPlatformIDs(16, platform_id, &ret_num_platforms);

    if (ret_num_platforms > 0) {
        printf("OpenCL GPUs detected:\n");
    } else {
        printf("No OpenCL platforms detected.\n");
    }

    for (int i = 0; i < ret_num_platforms; i++) {
        returnVal = clGetDeviceIDs(platform_id[i], CL_DEVICE_TYPE_GPU, 16, device_id, &numOpenCLDevices);
        for (int j = 0; j < numOpenCLDevices; j++) {
            returnVal = clGetDeviceInfo(
              device_id[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(globalMem), &globalMem, &sizeRet);
            returnVal = clGetDeviceInfo(
              device_id[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(computeUnits), &computeUnits, &sizeRet);
            printf("platform %d, device %d [memory %lu, compute units %d]\n", i, j, globalMem, computeUnits);
        }
    }
}

void CDynGPUKernel::initOpenCL(int platformID, int computeUnits, const std::vector<std::string>& program) {
    uint32_t largestMemgen = 0;
    uint32_t byteCodeLen = 0;
    std::vector<uint32_t> byteCode = executeAssembleByteCode(
      &largestMemgen, program, "0000", "0000"); // only calling to get largestMemgen
    CDynProgramGPU::byte_code_t byteCodePtr = vector_to_contiguous(byteCode);

    cl_int returnVal;
    cl_uint ret_num_platforms;

    cl_context* context = (cl_context*)malloc(16 * sizeof(cl_context));

    // Initialize context
    returnVal = clGetPlatformIDs(16, platform_id, &ret_num_platforms);
    returnVal = clGetDeviceIDs(platform_id[platformID], CL_DEVICE_TYPE_GPU, 16, openCLDevices, &numOpenCLDevices);
    for (int i = 0; i < numOpenCLDevices; i++) {
        context[i] = clCreateContext(NULL, 1, &openCLDevices[i], NULL, NULL, &returnVal);

        /*
        size_t sizeRet;
        cl_ulong globalMem;
        cl_ulong localMem;
        cl_uint computeUnits;
        size_t workGroups;
        cl_bool littleEndian;

        //Get some device capabilities
        returnVal = clGetDeviceInfo(device_id[deviceID], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(globalMem), &globalMem,
        &sizeRet); returnVal = clGetDeviceInfo(device_id[deviceID], CL_DEVICE_LOCAL_MEM_SIZE, sizeof(localMem),
        &localMem, &sizeRet); returnVal = clGetDeviceInfo(device_id[deviceID], CL_DEVICE_MAX_COMPUTE_UNITS,
        sizeof(computeUnits), &computeUnits, &sizeRet); returnVal = clGetDeviceInfo(device_id[deviceID],
        CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(workGroups), &workGroups, &sizeRet); returnVal =
        clGetDeviceInfo(device_id[deviceID], CL_DEVICE_ENDIAN_LITTLE, sizeof(littleEndian), &littleEndian, &sizeRet);
        */

        // computeUnits = numComputeUnits;

        // Read the kernel source
        FILE* kernelSourceFile;

        kernelSourceFile = fopen("dyn_miner.cl", "r");
        if (!kernelSourceFile) {
            fprintf(stderr, "Failed to load kernel.\n");
            return;
        }
        fseek(kernelSourceFile, 0, SEEK_END);
        size_t sourceFileLen = ftell(kernelSourceFile) + 1;
        char* kernelSource = (char*)malloc(sourceFileLen);
        memset(kernelSource, 0, sourceFileLen);
        fseek(kernelSourceFile, 0, SEEK_SET);
        fread(kernelSource, 1, sourceFileLen, kernelSourceFile);
        fclose(kernelSourceFile);

        cl_program program;

        // Create kernel program
        program = clCreateProgramWithSource(
          context[i], 1, (const char**)&kernelSource, (const size_t*)&sourceFileLen, &returnVal);
        returnVal = clBuildProgram(program, 1, &openCLDevices[i], NULL, NULL, NULL);
        free(kernelSource);

        if (returnVal == CL_BUILD_PROGRAM_FAILURE) {
            // Determine the size of the log
            size_t log_size;
            clGetProgramBuildInfo(program, openCLDevices[i], CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);

            // Allocate memory for the log
            char* log = (char*)malloc(log_size);

            // Get the log
            clGetProgramBuildInfo(program, openCLDevices[i], CL_PROGRAM_BUILD_LOG, log_size, log, NULL);

            // Print the log
            printf("\n\n%s\n", log);
        }

        kernel[i] = clCreateKernel(program, "dyn_hash", &returnVal);
        command_queue[i] = clCreateCommandQueueWithProperties(context[i], openCLDevices[i], NULL, &returnVal);

        // Calculate buffer sizes - mempool, hash result buffer, done flag
        uint32_t memgenBytes = largestMemgen * 32;
        uint32_t globalMempoolSize = memgenBytes * computeUnits;
        // TODO - make sure this is less than globalMem

        // Allocate program source buffer and load
        clGPUProgramBuffer[i] = clCreateBuffer(context[i], CL_MEM_READ_WRITE, byteCodeLen, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 0, sizeof(cl_mem), (void*)&clGPUProgramBuffer[i]);
        returnVal = clEnqueueWriteBuffer(
          command_queue[i], clGPUProgramBuffer[i], CL_TRUE, 0, byteCodePtr.size, byteCodePtr.ptr.get(), 0, NULL, NULL);

        // Allocate global memory buffer and zero
        cl_mem clGPUMemGenBuffer = clCreateBuffer(context[i], CL_MEM_READ_WRITE, globalMempoolSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 1, sizeof(cl_mem), (void*)&clGPUMemGenBuffer);
        /*
        unsigned char* buffMemGen = (unsigned char*)malloc(globalMempoolSize);
        memset(buffMemGen, 0, globalMempoolSize);
        returnVal = clEnqueueWriteBuffer(command_queue, clGPUMemGenBuffer, CL_TRUE, 0, globalMempoolSize, buffMemGen, 0,
        NULL, NULL);
        */

        // Size of memgen area - this is the number of 8 uint blocks
        returnVal = clSetKernelArg(kernel[i], 2, sizeof(largestMemgen), (void*)&largestMemgen);

        // Allocate hash result buffer and zero
        hashResultSize = computeUnits * 32;
        clGPUHashResultBuffer[i] = clCreateBuffer(context[i], CL_MEM_READ_WRITE, hashResultSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 3, sizeof(cl_mem), (void*)&clGPUHashResultBuffer[i]);
        buffHashResult[i] = (uint32_t*)malloc(hashResultSize);
        memset(buffHashResult[i], 0, hashResultSize);
        returnVal = clEnqueueWriteBuffer(
          command_queue[i], clGPUHashResultBuffer[i], CL_TRUE, 0, hashResultSize, buffHashResult[i], 0, NULL, NULL);

        /*
        //Allocate found flag buffer and zero
        uint32_t doneFlagSize = computeUnits;
        cl_mem clGPUDoneBuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, doneFlagSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel, 4, sizeof(clGPUDoneBuffer), (void*)&clGPUDoneBuffer);
        unsigned char* buffDoneFlag = (unsigned char*)malloc(doneFlagSize);
        memset(buffDoneFlag, 0, doneFlagSize);
        returnVal = clEnqueueWriteBuffer(command_queue, clGPUDoneBuffer, CL_TRUE, 0, doneFlagSize, buffDoneFlag, 0,
        NULL, NULL);
        */

        // Allocate header buffer and load
        headerBuffSize = computeUnits * 80;
        clGPUHeaderBuffer[i] = clCreateBuffer(context[i], CL_MEM_READ_WRITE, headerBuffSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 4, sizeof(cl_mem), (void*)&clGPUHeaderBuffer[i]);
        buffHeader[i] = (unsigned char*)malloc(headerBuffSize);
        memset(buffHeader[i], 0, headerBuffSize);
        returnVal = clEnqueueWriteBuffer(
          command_queue[i], clGPUHeaderBuffer[i], CL_TRUE, 0, headerBuffSize, buffHeader[i], 0, NULL, NULL);

        // Allocate SHA256 scratch buffer - this probably isnt needed if properly optimized
        uint32_t scratchBuffSize = computeUnits * 32;
        cl_mem clGPUScratchBuffer = clCreateBuffer(context[i], CL_MEM_READ_WRITE, scratchBuffSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 5, sizeof(cl_mem), (void*)&clGPUScratchBuffer);
        /*
        unsigned char* buffScratch = (unsigned char*)malloc(scratchBuffSize);
        memset(buffScratch, 0, scratchBuffSize);
        returnVal = clEnqueueWriteBuffer(command_queue, clGPUScratchBuffer, CL_TRUE, 0, scratchBuffSize, buffScratch, 0,
        NULL, NULL);
        */
    }
}

// returns 1 if timeout or 0 if successful
void CDynProgramGPU::startMiner(
  shared_work_t& shared_work,
  uint32_t numComputeUnits,
  uint32_t gpu,
  shares_t& shares,
  rand_seed_t rand_seed) { // WHISKERZ

    // assmeble bytecode for program
    // allocate global memory buffer based on largest size of memgen
    // allocate result hash buffer for each compute unit
    // allocate flag to indicate hash found for each compute unit (this is for later)
    // call kernel code with program, block header, memory buffer, result buffer and flag as params
    const work_t work = shared_work.clone();
    load_byte_code(work);
    cl_int returnVal;

    uint32_t nonce = rand_seed.rand_with_index(gpu);

    // copy header to GPU buffers
    for (int i = 0; i < numComputeUnits; i++) {
        memcpy(&kernel.buffHeader[gpu][i * 80], work.native_data, 80);
    }

    returnVal = clEnqueueWriteBuffer(
      kernel.command_queue[gpu],
      kernel.clGPUProgramBuffer[gpu],
      CL_TRUE,
      0,
      byte_code.size,
      byte_code.ptr.get(),
      0,
      NULL,
      NULL);

    while (shared_work == work) {
        // copy nonce to headers on GPU buffers
        for (int i = 0; i < numComputeUnits; i++) {
            uint32_t nonce1 = nonce + i;
            memcpy(&kernel.buffHeader[gpu][i * 80 + 76], &nonce1, 4);
        }
        returnVal = clEnqueueWriteBuffer(
          kernel.command_queue[gpu],
          kernel.clGPUHeaderBuffer[gpu],
          CL_TRUE,
          0,
          kernel.headerBuffSize,
          kernel.buffHeader[gpu],
          0,
          NULL,
          NULL);

        size_t globalWorkSize = numComputeUnits;
        size_t localWorkSize = 1;
        returnVal = clEnqueueNDRangeKernel(
          kernel.command_queue[gpu],
          kernel.kernel[gpu],
          1,
          NULL,
          &globalWorkSize,
          &localWorkSize,
          0,
          NULL,
          NULL);
        returnVal = clFinish(kernel.command_queue[gpu]);

        returnVal = clEnqueueReadBuffer(
          kernel.command_queue[gpu],
          kernel.clGPUHashResultBuffer[gpu],
          CL_TRUE,
          0,
          kernel.hashResultSize,
          kernel.buffHashResult[gpu],
          0,
          NULL,
          NULL);

        // find a hash with difficulty higher than share diff
        for (int k = 0; k < numComputeUnits; k++) {
            // read last 8 bytes of hash as [uint64_t] target
            uint64_t hash_int = kernel.buffHashResult[gpu][k * 8 + 3];
            // hash target should be lower than share target
            if (hash_int <= work.share_target) {
                // append share to queue
                shares.append(work.share(nonce + k));
            }
        }
        // increment local nonce
        nonce += numComputeUnits;
        // increment global atomic nonce counter
        shares.stats.nonce_count += numComputeUnits;
    }
}
