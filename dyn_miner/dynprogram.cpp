#include "dynprogram.h"
#ifdef _WIN32
// WHISKERZ CODE
#include "Windows.h"

#include <nlohmann/json.hpp>

#include <thread>
// WHISKERZ
#endif

std::string CDynProgram::execute(unsigned char* blockHeader, std::string prevBlockHash, std::string merkleRoot) {
    // initial input is SHA256 of header data

    CSHA256 ctx;

    uint32_t iResult[8];

    /*
    for (int i = 0; i < 80; i++)
        printf("%02X", blockHeader[i]);
    printf("\n");
    */

    ctx.Write(blockHeader, 80);
    ctx.Finalize((unsigned char*)iResult);

    /*
    for (int i = 0; i < 8; i++)
        printf("%08X", iResult[i]);
    printf("\n");
    */

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

            for (int i = 0; i < 8; i++)
                iResult[i] += arg1[i];

        }

        else if (tokens[0] == "XOR") {
            uint32_t arg1[8];
            parseHex(tokens[1], (unsigned char*)arg1);
            for (int i = 0; i < 8; i++)
                iResult[i] ^= arg1[i];
        }

        // hash algo which can be optionally repeated several times
        else if (tokens[0] == "SHA2") {
            if (tokens.size() == 2) { // includes a loop count
                loop_counter = atoi(tokens[1].c_str());
                for (int i = 0; i < loop_counter; i++) {
                    if (tokens[0] == "SHA2") {
                        unsigned char output[32];
                        ctx.Reset();
                        ctx.Write((unsigned char*)iResult, 32);
                        ctx.Finalize(output);
                        memcpy(iResult, output, 32);
                    }
                }
            }

            else { // just a single run
                if (tokens[0] == "SHA2") {
                    unsigned char output[32];
                    ctx.Reset();
                    ctx.Write((unsigned char*)iResult, 32);
                    ctx.Finalize(output);
                    memcpy(iResult, output, 32);
                }
            }
        }

        // generate a block of memory based on a hashing algo
        else if (tokens[0] == "MEMGEN") {
            if (memPool != NULL) free(memPool);
            memory_size = atoi(tokens[2].c_str());
            memPool = (uint32_t*)malloc(memory_size * 32);
            for (int i = 0; i < memory_size; i++) {
                if (tokens[1] == "SHA2") {
                    unsigned char output[32];
                    ctx.Reset();
                    ctx.Write((unsigned char*)iResult, 32);
                    ctx.Finalize(output);
                    memcpy(iResult, output, 32);
                    memcpy(memPool + i * 8, iResult, 32);
                }
            }
        }

        // add a constant to every value in the memory block
        else if (tokens[0] == "MEMADD") {
            if (memPool != NULL) {
                uint32_t arg1[8];
                parseHex(tokens[1], (unsigned char*)arg1);

                for (int i = 0; i < memory_size; i++) {
                    for (int j = 0; j < 8; j++)
                        memPool[i * 8 + j] += arg1[j];
                }
            }
        }

        // xor a constant with every value in the memory block
        else if (tokens[0] == "MEMXOR") {
            if (memPool != NULL) {
                uint32_t arg1[8];
                parseHex(tokens[1], (unsigned char*)arg1);

                for (int i = 0; i < memory_size; i++) {
                    for (int j = 0; j < 8; j++)
                        memPool[i * 8 + j] ^= arg1[j];
                }
            }
        }

        // read a value based on an index into the generated block of memory
        else if (tokens[0] == "READMEM") {
            if (memPool != NULL) {
                unsigned int index = 0;

                if (tokens[1] == "MERKLE") {
                    uint32_t arg1[8];
                    parseHex(merkleRoot, (unsigned char*)arg1);
                    index = arg1[0] % memory_size;
                    memcpy(iResult, memPool + index * 8, 32);
                }

                else if (tokens[1] == "HASHPREV") {
                    uint32_t arg1[8];
                    parseHex(prevBlockHash, (unsigned char*)arg1);
                    index = arg1[0] % memory_size;
                    memcpy(iResult, memPool + index * 8, 32);
                }
            }
        }

        /*
        printf("line %02d    ", line_ptr);
        unsigned char xx[32];
        memcpy(xx, iResult, 32);
        for (int i = 0; i < 32; i++)
            printf("%02X", xx[i]);
        printf("\n");
        */

        line_ptr++;
    }

    if (memPool != NULL) free(memPool);

    return makeHex((unsigned char*)iResult, 32);
}

std::string CDynProgram::getProgramString() {
    std::string result;

    for (int i = 0; i < program.size(); i++)
        result += program[i] + "\n";

    return result;
}

void CDynProgram::parseHex(std::string input, unsigned char* output) {
    for (int i = 0; i < input.length(); i += 2) {
        unsigned char value = decodeHex(input[i]) * 16 + decodeHex(input[i + 1]);
        output[i / 2] = value;
    }
}

unsigned char CDynProgram::decodeHex(char in) {
    in = toupper(in);
    if ((in >= '0') && (in <= '9'))
        return in - '0';
    else if ((in >= 'A') && (in <= 'F'))
        return in - 'A' + 10;
    else
        return 0; // todo raise error
}

std::string CDynProgram::makeHex(unsigned char* in, int len) {
    std::string result;
    for (int i = 0; i < len; i++) {
        result += hexDigit[in[i] / 16];
        result += hexDigit[in[i] % 16];
    }
    return result;
}

#ifdef GPU_MINER
void CDynProgram::initOpenCL(int platformID, int computeUnits) {
    uint32_t largestMemgen = 0;
    uint32_t byteCodeLen = 0;
    uint32_t* byteCode = executeGPUAssembleByteCode(
      &largestMemgen, "0000", "0000", &byteCodeLen); // only calling to get largestMemgen

    cl_int returnVal;
    cl_platform_id* platform_id = (cl_platform_id*)malloc(16 * sizeof(cl_platform_id));
    openCLDevices = (cl_device_id*)malloc(16 * sizeof(cl_device_id));
    cl_uint ret_num_platforms;
    cl_context* context = (cl_context*)malloc(16 * sizeof(cl_context));
    kernel = (cl_kernel*)malloc(16 * sizeof(cl_kernel));
    command_queue = (cl_command_queue*)malloc(16 * sizeof(cl_command_queue));

    clGPUHashResultBuffer = (cl_mem*)malloc(16 * sizeof(cl_mem));
    buffHashResult = (uint32_t**)malloc(16 * sizeof(uint32_t*));

    clGPUHeaderBuffer = (cl_mem*)malloc(16 * sizeof(cl_mem));
    buffHeader = (unsigned char**)malloc(16 * sizeof(char*));

    clGPUProgramBuffer = (cl_mem*)malloc(16 * sizeof(cl_mem));

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
        returnVal = clSetKernelArg(kernel[i], 0, sizeof(clGPUProgramBuffer[i]), (void*)&clGPUProgramBuffer[i]);
        returnVal = clEnqueueWriteBuffer(
          command_queue[i], clGPUProgramBuffer[i], CL_TRUE, 0, byteCodeLen, byteCode, 0, NULL, NULL);

        // Allocate global memory buffer and zero
        cl_mem clGPUMemGenBuffer = clCreateBuffer(context[i], CL_MEM_READ_WRITE, globalMempoolSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 1, sizeof(clGPUMemGenBuffer), (void*)&clGPUMemGenBuffer);
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
        returnVal = clSetKernelArg(kernel[i], 3, sizeof(clGPUHashResultBuffer[i]), (void*)&clGPUHashResultBuffer[i]);
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
        returnVal = clSetKernelArg(kernel[i], 4, sizeof(clGPUHeaderBuffer[i]), (void*)&clGPUHeaderBuffer[i]);
        buffHeader[i] = (unsigned char*)malloc(headerBuffSize);
        memset(buffHeader[i], 0, headerBuffSize);
        returnVal = clEnqueueWriteBuffer(
          command_queue[i], clGPUHeaderBuffer[i], CL_TRUE, 0, headerBuffSize, buffHeader[i], 0, NULL, NULL);

        // Allocate SHA256 scratch buffer - this probably isnt needed if properly optimized
        uint32_t scratchBuffSize = computeUnits * 32;
        cl_mem clGPUScratchBuffer = clCreateBuffer(context[i], CL_MEM_READ_WRITE, scratchBuffSize, NULL, &returnVal);
        returnVal = clSetKernelArg(kernel[i], 5, sizeof(clGPUScratchBuffer), (void*)&clGPUScratchBuffer);
        /*
        unsigned char* buffScratch = (unsigned char*)malloc(scratchBuffSize);
        memset(buffScratch, 0, scratchBuffSize);
        returnVal = clEnqueueWriteBuffer(command_queue, clGPUScratchBuffer, CL_TRUE, 0, scratchBuffSize, buffScratch, 0,
        NULL, NULL);
        */
    }
}

// returns 1 if timeout or 0 if successful
int CDynProgram::executeGPU(
  unsigned char* blockHeader,
  std::string prevBlockHash,
  std::string merkleRoot,
  unsigned char* nativeTarget,
  uint32_t* resultNonce,
  int numComputeUnits,
  uint32_t serverNonce,
  int gpuIndex,
  CDynProgram* dynProgram) { // WHISKERZ

    // assmeble bytecode for program
    // allocate global memory buffer based on largest size of memgen
    // allocate result hash buffer for each compute unit
    // allocate flag to indicate hash found for each compute unit (this is for later)
    // call kernel code with program, block header, memory buffer, result buffer and flag as params

    uint32_t junk;
    uint32_t byteCodeLen = 0;
    uint32_t* byteCode = executeGPUAssembleByteCode(&junk, prevBlockHash, merkleRoot, &byteCodeLen);

    cl_int returnVal;

    returnVal = clEnqueueWriteBuffer(
      command_queue[gpuIndex], clGPUProgramBuffer[gpuIndex], CL_TRUE, 0, byteCodeLen, byteCode, 0, NULL, NULL);

    time_t start;
    time(&start);
    time_t lastreport = start;

    for (int i = 0; i < numComputeUnits; i++)
        memcpy(&buffHeader[gpuIndex][i * 80], blockHeader, 80);

    int loops = 0;
    srand(start);
    uint32_t nonce = serverNonce;

    bool found = false;

    unsigned char hashA[32];
    unsigned char best[32];
    memset(best, 255, 32);

    int foundIndex = -1;

    uint32_t startNonce = nonce;
    dynProgram->timeout = false;                                                       // WHISKERZ
    while ((!found) && (!dynProgram->timeout) && (!globalFound) && (!globalTimeout)) { // WHISKERZ

        for (int i = 0; i < numComputeUnits; i++) {
            uint32_t nonce1 = nonce + i;
            memcpy(&buffHeader[gpuIndex][i * 80 + 76], &nonce1, 4);
        }

        returnVal = clEnqueueWriteBuffer(
          command_queue[gpuIndex],
          clGPUHeaderBuffer[gpuIndex],
          CL_TRUE,
          0,
          headerBuffSize,
          buffHeader[gpuIndex],
          0,
          NULL,
          NULL);

        size_t globalWorkSize = numComputeUnits;
        size_t localWorkSize = 1;
        returnVal = clEnqueueNDRangeKernel(
          command_queue[gpuIndex], kernel[gpuIndex], 1, NULL, &globalWorkSize, &localWorkSize, 0, NULL, NULL);
        returnVal = clFinish(command_queue[gpuIndex]);

        returnVal = clEnqueueReadBuffer(
          command_queue[gpuIndex],
          clGPUHashResultBuffer[gpuIndex],
          CL_TRUE,
          0,
          hashResultSize,
          buffHashResult[gpuIndex],
          0,
          NULL,
          NULL);

        int k = 0;
        while ((!found) && (k < numComputeUnits)) {
            bool ok = false;
            bool done = false;
            int i = 0;

            memcpy(hashA, &buffHashResult[gpuIndex][k * 8], 32);

            while ((!ok) && (i < 32) && (!done))
                if (hashA[i] < nativeTarget[i])
                    ok = true;
                else if (hashA[i] == nativeTarget[i])
                    i++;
                else
                    done = true;

            bool better = false;
            done = false;
            i = 0;
            while ((!better) && (i < 32) && (!done))
                if (hashA[i] < best[i])
                    better = true;
                else if (hashA[i] == best[i])
                    i++;
                else
                    done = true;

            if (better) memcpy(best, hashA, 32);

            if (ok)
                found = true;
            else
                k++;
        }

        time_t now;
        time(&now);
        if (dynProgram->checkingHeight == false) {
            std::thread([&]() { checkBlockHeight(dynProgram); }).detach();
        } // WHISKERZ
        if ((now - lastreport) >= 3) {
            time_t current;
            time(&current);
            long long diff = current - start;
            // printf("GPU %d hashrate: %8.2f\n", gpuIndex, (float)(nonce - startNonce) / float(diff));
            dynProgram->outputStats(dynProgram, current, start, (nonce - startNonce)); // WHISKERZ

            // if (now - start > 18) {
            // dynProgram->timeout = true; //WHISKERZ
            // printf("Checking for stale block\n");
            //}

            lastreport = now;
        }
        loops++;

        if (found) {
            foundIndex = k;
            globalFound = true;
        } else {
            nonce += numComputeUnits;
            globalNonceCount += numComputeUnits;
        }
    }

    if (foundIndex != -1) {
        memcpy(resultNonce, &buffHeader[gpuIndex][foundIndex * 80 + 76], 4);
        return true;
    }

    return false;
}

uint32_t* CDynProgram::executeGPUAssembleByteCode(
  uint32_t* largestMemgen, std::string prevBlockHash, std::string merkleRoot, uint32_t* byteCodeLen) {
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
                uint32_t arg1[8];
                parseHex(merkleRoot, (unsigned char*)arg1);
                code.push_back(arg1[0]);
            }

            else if (tokens[1] == "HASHPREV") {
                uint32_t arg1[8];
                parseHex(prevBlockHash, (unsigned char*)arg1);
                code.push_back(arg1[0]);
            }
        }

        line_ptr++;
    }

    code.push_back(HASHOP_END);

    uint32_t* result = (uint32_t*)malloc(sizeof(uint32_t) * code.size());
    for (int i = 0; i < code.size(); i++)
        result[i] = code.at(i);

    *byteCodeLen = code.size() * 4;

    return result;
}
#endif

/*
// WHISKERZ CODE:

struct MemoryStruct {
    char* memory;
    size_t size;
};

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

    char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

bool CDynProgram::checkBlockHeight(CDynProgram* dynProgram)
{
    dynProgram->checkingHeight = true;
    CURL* curl;
    CURLcode res;

    //printf("Checking for stale block!\n");

    nlohmann::json j = { {"id", 0}, {"method","getblocktemplate"}, { "params", {{{"rules", {"segwit"}} }} } };
    std::string jData = j.dump();

    struct MemoryStruct chunk;
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, dynProgram->strRPC_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, dynProgram->RPCUser);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, dynProgram->RPCPassword);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jData.c_str());
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    else {
        nlohmann::json result = nlohmann::json::parse(chunk.memory);
        uint32_t chainHeight = result["result"]["height"];
        //printf("Chain Block is at: %d, we're on %d\n", chainHeight, dynProgram->height);
        if (dynProgram->height != chainHeight) {
            printf("Block %d has gone stale, switching to current block %d\n", dynProgram->height, chainHeight);
            dynProgram->timeout = true;
            globalTimeout = true;
        }
    }

    Sleep(1000);
    dynProgram->checkingHeight = false;
    return(true);
}
*/

#ifdef _WIN32
static std::string convertSecondsToUptime(int n);

bool CDynProgram::outputStats(CDynProgram* dynProgram, time_t now, time_t start, uint32_t nonce) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    struct tm* timeinfo;
    char timestamp[80];
    timeinfo = localtime(&now);
    strftime(timestamp, 80, "%F %T", timeinfo);
    char rateDisplay[256];
    float hashrate = (float)nonce / (float)(now - start);
    if (hashrate >= tb)
        sprintf(rateDisplay, "%.2f TH/s", (float)hashrate / tb);
    else if (hashrate >= gb && hashrate < tb)
        sprintf(rateDisplay, "%.2f GH/s", (float)hashrate / gb);
    else if (hashrate >= mb && hashrate < gb)
        sprintf(rateDisplay, "%.2f MH/s", (float)hashrate / mb);
    else if (hashrate >= kb && hashrate < mb)
        sprintf(rateDisplay, "%.2f KH/s", (float)hashrate / kb);
    else if (hashrate < kb)
        sprintf(rateDisplay, "%.2f H/s ", hashrate);
    else
        sprintf(rateDisplay, "%.2f H/s", hashrate);

    int uptimeSeconds = difftime(now, dynProgram->miningStartTime);
    std::string uptime = convertSecondsToUptime(uptimeSeconds);
    float uptimeMinutes = (float)uptimeSeconds / (float)60;
    float coinsPerMinute = 0;
    if (dynProgram->acceptedBlocks > 0) {
        coinsPerMinute = (float)dynProgram->acceptedBlocks / uptimeMinutes;
    } else {
        coinsPerMinute = 0;
    }

    SetConsoleTextAttribute(hConsole, LIGHTBLUE);
    printf("%s: ", timestamp);
    SetConsoleTextAttribute(hConsole, GREEN);
    printf("%s", rateDisplay);
    SetConsoleTextAttribute(hConsole, LIGHTGRAY);
    printf(" | ");
    SetConsoleTextAttribute(hConsole, LIGHTGREEN);
    printf("%d", dynProgram->height);
    SetConsoleTextAttribute(hConsole, LIGHTGRAY);
    printf(" | ");
    SetConsoleTextAttribute(hConsole, BLUE);
    printf("Uptime:%s", uptime.c_str());
    SetConsoleTextAttribute(hConsole, LIGHTGRAY);
    printf(" | ");
    SetConsoleTextAttribute(hConsole, LIGHTGREEN);
    printf("A:%d", dynProgram->acceptedBlocks);
    SetConsoleTextAttribute(hConsole, GREEN);
    printf(" (%.2f/m)", coinsPerMinute);
    SetConsoleTextAttribute(hConsole, RED);
    printf(" R:%d", dynProgram->rejectedBlocks);
    SetConsoleTextAttribute(hConsole, LIGHTGRAY);
    printf(" | ");
    SetConsoleTextAttribute(hConsole, LIGHTMAGENTA);
    printf("DynMiner %s %s\n", minerVersion, dynProgram->minerType);
    SetConsoleTextAttribute(hConsole, LIGHTGRAY);

    return (true);
}

static std::string convertSecondsToUptime(int n) {
    int days = n / (24 * 3600);

    n = n % (24 * 3600);
    int hours = n / 3600;

    n %= 3600;
    int minutes = n / 60;

    n %= 60;
    int seconds = n;

    std::string uptimeString;
    if (days > 0) {
        uptimeString = std::to_string(days) + "d" + std::to_string(hours) + "h" + std::to_string(minutes) + "m"
                       + std::to_string(seconds) + "s";
    } else if (hours > 0) {
        uptimeString = std::to_string(hours) + "h" + std::to_string(minutes) + "m" + std::to_string(seconds) + "s";
    } else if (minutes > 0) {
        uptimeString = std::to_string(minutes) + "m" + std::to_string(seconds) + "s";
    } else {
        uptimeString = std::to_string(seconds) + "s";
    }

    return (uptimeString);
}
#endif
// END WHISKERZ
