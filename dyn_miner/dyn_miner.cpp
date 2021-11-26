// dyn_miner.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "common.h"
#include "dynhash.h"
#include "sha256.h"

#include <nlohmann/json.hpp>

#ifdef __linux__
#include <linux/kernel.h> /* for struct sysinfo */
#include <linux/unistd.h> /* for _syscallX macros/related stuff */
#include <sys/sysinfo.h>
#endif

#ifdef _WIN32
//#include "process.h"
#endif

#include <iostream>
#include <thread>

void diff_to_target(uint32_t* target, double diff);
void bin2hex(char* s, const unsigned char* p, size_t len);
void memrev(unsigned char* p, size_t len);
bool hex2bin(unsigned char* p, const char* hexstr, size_t len);
int varint_encode(unsigned char* p, uint64_t n);
static bool b58dec(unsigned char* bin, size_t binsz, const char* b58);
static int b58check(unsigned char* bin, size_t binsz, const char* b58);
size_t address_to_script(unsigned char* out, size_t outsz, const char* addr);
extern void sha256d(unsigned char* hash, const unsigned char* data, int len);

// WHISKERZ CODE
#ifdef _WIN32
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
// bool checkBlockHeight(CDynProgram*);
// bool validateSubmission(CDynProgram*, uint32_t);
// bool checkingHeightCPU = false;
CDynProgram* dynProgram;
// WHISKERZ

struct MemoryStruct {
    char* memory;
    size_t size;
};

typedef unsigned char tree_entry[32];

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

    char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

bool globalFound;
bool globalTimeout;
uint32_t globalNonceCount;

CDynHash* hashFunction;
std::string prevBlockHash;
char strMerkleRoot[128];
std::string strNativeTarget;
uint32_t iNativeTarget[8];
unsigned char nativeTarget[32];
unsigned char nativeData[80];

int numCPUThreads;
int GPUplatformID;

uint32_t serverNonce; // nonce from pool server, if used

#ifdef GPU_MINER
void doGPUHash(int gpuIndex, unsigned char* result) {
    uint32_t resultNonce;

    unsigned char header[80];
    memcpy(header, nativeData, 80);

    time_t now;
    time(&now);

    if (hashFunction->programs[0]->executeGPU(
          header,
          prevBlockHash,
          strMerkleRoot,
          nativeTarget,
          &resultNonce,
          numCPUThreads,
          serverNonce + gpuIndex * now,
          gpuIndex,
          dynProgram)) {
        // printf("GPU %d found nonce %d\n", gpuIndex, resultNonce);
        memcpy(header + 76, &resultNonce, 4);
        memcpy(result, header, 80);
    }
}
#endif

void doHash(void* result) {
    time_t t;
    time(&t);
    srand(t);

#ifdef _WIN32
    uint32_t nonce = rand() * t * GetTickCount();
#endif

#ifdef __linux__
    uint32_t nonce = rand() * t;
#endif

    unsigned char header[80];
    memcpy(header, nativeData, 80);
    memcpy(header + 76, &nonce, 4);

    unsigned char hashA[32];
    unsigned char best[32];
    memset(best, 255, 32);

    time_t start;
    time(&start);

    uint32_t startNonce = nonce;

    bool found = false;
    while ((!found) && (!globalFound) && (!globalTimeout)) {
        std::string result = hashFunction->programs[0]->execute(
          header, prevBlockHash, strMerkleRoot); // todo - overwrites local param "result"
        hex2bin(hashA, result.c_str(), 32);

        bool ok = false;
        bool done = false;
        int i = 0;
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

        /*
        if (nonce % 10000 == 0) {
            printf("%d\n", nonce);
            for (int i = 0; i < 32; i++)
                printf("%02X", best[i]);
            printf("\n");
            for (int i = 0; i < 32; i++)
                printf("%02X", nativeTarget[i]);
            printf("\n\n");
        }
        */

        if (ok)
            found = true;
        else {
            nonce++;
            memcpy(header + 76, &nonce, 4);
        }

        time_t current;
        time(&current);
        long long diff = current - start;

        if (diff % 3 == 0) {
            globalNonceCount += nonce - startNonce;
            startNonce = nonce;
        }
    }

    if (found) memcpy(result, header, 80);

    globalFound = true;
}

int main(int argc, char* argv[]) {
    /*
    // Get current flag
    int tmpFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    tmpFlag |= _CRTDBG_LEAK_CHECK_DF;
    tmpFlag |= _CRTDBG_CHECK_CRT_DF;
    tmpFlag |= _CRTDBG_ALLOC_MEM_DF;
    tmpFlag |= _CRTDBG_CHECK_ALWAYS_DF;
    _CrtSetDbgFlag(tmpFlag);

    printf("%d\n", _CrtCheckMemory());

    char* test = (char*)malloc(10);
    test[9] = 0;

    //printf("%d\n", _CrtCheckMemory());
    */

    printf("*******************************************************************\n");
    printf("Dynamo coin reference miner.  This software is supplied by Dynamo\n");
    printf("Coin Foundation with no warranty and solely on an AS-IS basis.\n");
    printf("\n");
    printf("We hope others will use this as a code base to produce production\n");
    printf("quality mining software.\n");
    printf("\n");
    printf("Version %s, July 4, 2021\n", minerVersion);
    printf("*******************************************************************\n");

    /*
    printf("args=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("arg%d=%s\n", i, argv[i]);
    printf("\n");
    */

    /*
     * TODO(crackcomm): GPU support
    cl_int returnVal;
    cl_platform_id* platform_id = (cl_platform_id*)malloc(16 * sizeof(cl_platform_id));
    cl_device_id* device_id = (cl_device_id*)malloc(16 * sizeof(cl_device_id));
    cl_uint ret_num_devices;
    cl_uint ret_num_platforms;
    cl_context context;

    cl_ulong globalMem;
    cl_ulong localMem;
    cl_uint computeUnits;
    size_t sizeRet;

    printf("OpenCL GPUs detected:\n");
    //Initialize context
    returnVal = clGetPlatformIDs(16, platform_id, &ret_num_platforms);
    for (int i = 0; i < ret_num_platforms; i++) {
        returnVal = clGetDeviceIDs(platform_id[i], CL_DEVICE_TYPE_GPU, 16, device_id, &ret_num_devices);
        for (int j = 0; j < ret_num_devices; j++) {
            returnVal = clGetDeviceInfo(device_id[j], CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(globalMem), &globalMem,
    &sizeRet); returnVal = clGetDeviceInfo(device_id[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(computeUnits),
    &computeUnits, &sizeRet); printf("platform %d, device %d [memory %lu, compute units %d]\n", i, j, globalMem,
    computeUnits);
        }
    }

    printf("\n");
    */

    if (argc != 9) {
        printf("usage: dyn_miner <RPC URL> <RPC username> <RPC password> <miner pay to address> <CPU|GPU> <num CPU "
               "threads|num GPU compute units> <gpu platform id> <pool | solo>\n\n");
        printf("EXAMPLE:\n");
        printf("    dyn_miner http://testnet1.dynamocoin.org:6433 user 123456 "
               "dy1qxj4awv48k7nelvwwserdl9wha2mfg6w3wy05fc CPU 4 0 pool\n");
        printf("    dyn_miner http://testnet1.dynamocoin.org:6433 user 123456 "
               "dy1qxj4awv48k7nelvwwserdl9wha2mfg6w3wy05fc GPU 1000 0 solo\n");
        printf("\n");
        printf("In CPU mode the program will create N number of CPU threads.\nIn GPU mode, the program will create N "
               "number of compute units.\n");
        printf("platform ID (starts at 0) is for multi GPU systems.  Ignored for CPU.\n");
        printf("pool mode enables use with dyn miner pool, solo is for standalone mining.\n");

        return -1;
    }

    dynProgram = new CDynProgram();

    dynProgram->strRPC_URL = argv[1];
    dynProgram->RPCUser = argv[2];
    dynProgram->RPCPassword = argv[3];
    dynProgram->minerPayToAddr = argv[4];
    std::string poolShareWallet = std::string(
      argv[4]); // save miner's wallet addr in another place if they are mining against pool because we replace it
    dynProgram->minerType = argv[5];
    numCPUThreads = atoi(argv[6]);
    GPUplatformID = atoi(argv[7]);
    char* mode = argv[8];

    if ((toupper(dynProgram->minerType[0]) != 'C') && (toupper(dynProgram->minerType[0]) != 'G')) {
        printf("Miner type must be CPU or GPU");
    }

    time(&dynProgram->miningStartTime);

    using json = nlohmann::json;

    struct MemoryStruct chunk;

    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    hashFunction = new CDynHash();

    while (true) {
        if (curl) {
            time_t t;
            time(&t);
            serverNonce = t; // if no pool use epoch for GPU nonce  TODO - can get more entropy here
            if (strcmp(mode, "pool") == 0) {
                std::string getHashRequest = std::string(
                  "{ \"id\": 0, \"method\" : \"getpooldata\", \"params\" : [] }");

                chunk.size = 0;

                curl_easy_setopt(curl, CURLOPT_URL, dynProgram->strRPC_URL);
                curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
                curl_easy_setopt(curl, CURLOPT_USERNAME, dynProgram->RPCUser);
                curl_easy_setopt(curl, CURLOPT_PASSWORD, dynProgram->RPCPassword);

                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, getHashRequest.c_str());

                res = curl_easy_perform(curl);

                if (res != CURLE_OK)
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                else {
                    json result = json::parse(chunk.memory);
                    std::string strWallet = result["walletAddr"];
                    serverNonce = result["nonce"];
                    dynProgram->minerPayToAddr = strWallet;
                }

                chunk.size = 0;
            }

            std::string getHashRequest = std::string(
              "{ \"id\": 0, \"method\" : \"gethashfunction\", \"params\" : [] }");

            chunk.size = 0;

            curl_easy_setopt(curl, CURLOPT_URL, dynProgram->strRPC_URL);
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, dynProgram->RPCUser);
            curl_easy_setopt(curl, CURLOPT_PASSWORD, dynProgram->RPCPassword);

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, getHashRequest.c_str());

            res = curl_easy_perform(curl);

            if (res != CURLE_OK)
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            else {
                json result = json::parse(chunk.memory);
                // printf("%s\n", result.dump().c_str());
                int start_time = result["result"][0]["start_time"];
                std::string program = result["result"][0]["program"];

                // if we are using GPU mode and this is the first time we are loading the program, then init the kernel
                bool initGPU = false;
                if (toupper(dynProgram->minerType[0]) == 'G') {
                    if (hashFunction->programs.empty()) initGPU = true;
                }
                hashFunction->addProgram(start_time, program);
                if (initGPU) {
                    hashFunction->programs[0]->initOpenCL(GPUplatformID, numCPUThreads);
                }
            }

            chunk.size = 0;

            json j
              = "{ \"id\": 0, \"method\" : \"getblocktemplate\", \"params\" : [{ \"rules\": [\"segwit\"] }] }"_json;
            std::string jData = j.dump();

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
                json result = json::parse(chunk.memory);
                // printf("%s\n", result.dump().c_str());

                dynProgram->height = result["result"]["height"];
                uint32_t version = result["result"]["version"];
                prevBlockHash = result["result"]["previousblockhash"];
                int64_t coinbaseVal = result["result"]["coinbasevalue"];
                uint32_t curtime = result["result"]["curtime"];
                std::string difficultyBits = result["result"]["bits"];
                json jtransactions = result["result"]["transactions"];
                strNativeTarget = result["result"]["target"];

                int tx_size = 0;
                for (int i = 0; i < jtransactions.size(); i++) {
                    std::string strTransaction = jtransactions[i]["data"];
                    tx_size += strlen(strTransaction.c_str()) / 2;
                }

                int tx_count = jtransactions.size();

                // decode pay to address for miner
                static unsigned char pk_script[25] = {0};
                std::string payToAddress(dynProgram->minerPayToAddr);
                int pk_script_size = address_to_script(pk_script, sizeof(pk_script), payToAddress.c_str());

                // decode pay to address for developer
                static unsigned char pk_script_dev[25] = {0};
                std::string payToAddressDev("dy1qzvx3yfrucqa2ntsw8e7dyzv6u6dl2c2wjvx5jy");
                int pk_script_size_dev = address_to_script(
                  pk_script_dev, sizeof(pk_script_dev), payToAddressDev.c_str());

                static unsigned char pk_script_charity[25] = {0};
                std::string payToAddressCharity("dy1qnt3gjkefzez7my4zmwx9w0xs3c2jcxks6kxrgp");
                int pk_script_size_charity = address_to_script(
                  pk_script_charity, sizeof(pk_script_charity), payToAddressCharity.c_str());

                // create coinbase transaction

                unsigned char cbtx[512];
                memset(cbtx, 0, 512);
                le32enc((uint32_t*)cbtx, 1);                 // version
                cbtx[4] = 1;                                 // txin count
                memset(cbtx + 5, 0x00, 32);                  // prev txn hash out
                le32enc((uint32_t*)(cbtx + 37), 0xffffffff); // prev txn index out
                int cbtx_size = 43;

                for (int n = dynProgram->height; n; n >>= 8) {
                    cbtx[cbtx_size++] = n & 0xff;
                    if (n < 0x100 && n >= 0x80) cbtx[cbtx_size++] = 0;
                }
                cbtx[42] = cbtx_size - 43;

                cbtx[41] = cbtx_size - 42;                          // script signature length
                le32enc((uint32_t*)(cbtx + cbtx_size), 0xffffffff); // out sequence
                cbtx_size += 4;

                cbtx[cbtx_size++] = 4; // out count - one txout to devfee, one txout to miner, one txout for charity,
                                       // one for witness sighash

                // coinbase to miner
                le32enc((uint32_t*)(cbtx + cbtx_size), (uint32_t)coinbaseVal); // tx out amount
                le32enc((uint32_t*)(cbtx + cbtx_size + 4), coinbaseVal >> 32);
                cbtx_size += 8;
                cbtx[cbtx_size++] = pk_script_size; // tx out script len
                memcpy(cbtx + cbtx_size, pk_script, pk_script_size);
                cbtx_size += pk_script_size;

                // coinbase to developer
                int64_t devFee = 5000000;
                le32enc((uint32_t*)(cbtx + cbtx_size), (uint32_t)devFee); // tx out amount
                le32enc((uint32_t*)(cbtx + cbtx_size + 4), devFee >> 32);
                cbtx_size += 8;
                cbtx[cbtx_size++] = pk_script_size_dev; // tx out script len
                memcpy(cbtx + cbtx_size, pk_script_dev, pk_script_size_dev);
                cbtx_size += pk_script_size_dev;

                // coinbase to charity
                int64_t charityFee = 5000000;
                le32enc((uint32_t*)(cbtx + cbtx_size), (uint32_t)charityFee); // tx out amount
                le32enc((uint32_t*)(cbtx + cbtx_size + 4), charityFee >> 32);
                cbtx_size += 8;
                cbtx[cbtx_size++] = pk_script_size_charity; // tx out script len
                memcpy(cbtx + cbtx_size, pk_script_charity, pk_script_size_charity);
                cbtx_size += pk_script_size_charity;

                // execute all contract calls
                // create new coinbase transactions
                // update contract state and storage

                tree_entry* wtree = (tree_entry*)malloc((tx_count + 3) * 32);
                memset(wtree, 0, (tx_count + 3) * 32);

                memset(cbtx + cbtx_size, 0, 8); // value of segwit txout
                cbtx_size += 8;
                cbtx[cbtx_size++] = 38;   // tx out script length
                cbtx[cbtx_size++] = 0x6a; // txout script
                cbtx[cbtx_size++] = 0x24;
                cbtx[cbtx_size++] = 0xaa;
                cbtx[cbtx_size++] = 0x21;
                cbtx[cbtx_size++] = 0xa9;
                cbtx[cbtx_size++] = 0xed;

                for (int i = 0; i < jtransactions.size(); i++) {
                    std::string strTransactionHash = jtransactions[i]["hash"];
                    hex2bin(wtree[i + 1], strTransactionHash.c_str(), 32);
                    memrev(wtree[1 + i], 32);
                }
                /*
                for (int i = 0; i < tx_count; i++) {
                    const json_t* tx = json_array_get(txa, i);
                    const json_t* hash = json_object_get(tx, "hash");
                    if (!hash || !hex2bin(wtree[1 + i], json_string_value(hash), 32)) {
                        applog(LOG_ERR, "JSON invalid transaction hash");
                        free(wtree);
                        goto out;
                    }
                    memrev(wtree[1 + i], 32);
                }
                */

                int n = tx_count + 1;
                while (n > 1) {
                    if (n % 2) memcpy(wtree[n], wtree[n - 1], 32);
                    n = (n + 1) / 2;
                    for (int i = 0; i < n; i++)
                        sha256d(wtree[i], wtree[2 * i], 64);
                }

                memset(wtree[1], 0, 32); // witness reserved value = 0
                sha256d(cbtx + cbtx_size, wtree[0], 64);

                cbtx_size += 32;
                free(wtree);

                le32enc((uint32_t*)(cbtx + cbtx_size), 0); //  tx out lock time
                cbtx_size += 4;

                unsigned char txc_vi[9];
                char* transactionString;

                n = varint_encode(txc_vi, 1 + tx_count);
                transactionString = (char*)malloc(2 * (n + cbtx_size + tx_size) + 2);
                memset(transactionString, 0, 2 * (n + cbtx_size + tx_size) + 2);
                bin2hex(transactionString, txc_vi, n);
                bin2hex(transactionString + 2 * n, cbtx, cbtx_size);
                char* txs_end = transactionString + strlen(transactionString);

                // create merkle root

                tree_entry* merkle_tree = (tree_entry*)malloc(32 * ((1 + tx_count + 1) & ~1));
                // size_t tx_buf_size = 32 * 1024;
                // unsigned char *tx = (unsigned char*)malloc(tx_buf_size);
                sha256d(merkle_tree[0], cbtx, cbtx_size);

                for (int i = 0; i < tx_count; i++) {
                    std::string tx_hex = jtransactions[i]["data"];
                    const size_t tx_hex_len = tx_hex.length();
                    const int tx_size = tx_hex_len / 2;
                    std::string txid = jtransactions[i]["txid"];
                    hex2bin(merkle_tree[1 + i], txid.c_str(), 32);
                    memrev(merkle_tree[1 + i], 32);
                    memcpy(txs_end, tx_hex.c_str(), tx_hex.length());
                    txs_end += tx_hex_len;
                }

                // free(tx);
                // tx = NULL;

                n = 1 + tx_count;
                while (n > 1) {
                    if (n % 2) {
                        memcpy(merkle_tree[n], merkle_tree[n - 1], 32);
                        ++n;
                    }
                    n /= 2;
                    for (int i = 0; i < n; i++)
                        sha256d(merkle_tree[i], merkle_tree[2 * i], 64);
                }

                // assemble header

                uint32_t headerData[32];
                version = 0x04000000;
                headerData[0] = swab32(version);

                uint32_t prevhash[8];
                hex2bin((unsigned char*)&prevhash, prevBlockHash.c_str(), 32);
                for (int i = 0; i < 8; i++)
                    headerData[8 - i] = le32dec(prevhash + i);

                for (int i = 0; i < 8; i++)
                    headerData[9 + i] = be32dec((uint32_t*)merkle_tree[0] + i);

                headerData[17] = swab32(curtime);

                uint32_t bits;
                hex2bin((unsigned char*)&bits, difficultyBits.c_str(), 4);
                headerData[18] = le32dec(&bits);

                memset(headerData + 19, 0x00, 52);

                headerData[20] = 0x80000000;
                headerData[31] = 0x00000280;

                // set up variables for the miner

                unsigned char cVersion[4];
                memcpy(cVersion, &version, 4);
                for (int i = 0; i < 4; i++)
                    cVersion[i] = ((cVersion[i] & 0x0F) << 4) + (cVersion[i] >> 4);

                memcpy(nativeData, &cVersion[3], 1);
                memcpy(nativeData + 1, &cVersion[2], 1);
                memcpy(nativeData + 2, &cVersion[1], 1);
                memcpy(nativeData + 3, &cVersion[0], 1);

                memcpy(nativeData + 4, prevhash, 32);

                memcpy(nativeData + 36, merkle_tree[0], 32);

                memcpy(nativeData + 68, &curtime, 4);

                unsigned char cBits[4];
                memcpy(cBits, &bits, 4);
                memcpy(nativeData + 72, &cBits[3], 1);
                memcpy(nativeData + 73, &cBits[2], 1);
                memcpy(nativeData + 74, &cBits[1], 1);
                memcpy(nativeData + 75, &cBits[0], 1);

                hex2bin((unsigned char*)&iNativeTarget, strNativeTarget.c_str(), 32);

                memcpy(&nativeTarget, &iNativeTarget, 32);

                // solve block

                // reverse merkle root...why?  because bitcoin
                unsigned char revMerkleRoot[32];
                memcpy(revMerkleRoot, merkle_tree[0], 32);
                for (int i = 0; i < 16; i++) {
                    unsigned char tmp = revMerkleRoot[i];
                    revMerkleRoot[i] = revMerkleRoot[31 - i];
                    revMerkleRoot[31 - i] = tmp;
                }
                bin2hex(strMerkleRoot, revMerkleRoot, 32);

                unsigned char header[80];

                globalFound = false;
                globalTimeout = false;
                globalNonceCount = 0;

                if (toupper(dynProgram->minerType[0]) == 'C') {
                    // CPU miner
                    for (int i = 0; i < numCPUThreads; i++) {
                        std::thread t1(doHash, header);
                        std::this_thread::sleep_for(std::chrono::milliseconds(strMerkleRoot[10]));
                        t1.detach();
                        //                        _beginthread(doHash, 0, header);
                        //                          Sleep((strMerkleRoot[10] * GetTickCount()) % 23);
                    }

                    time_t start;
                    time(&start);

                    while ((!globalFound) && (!globalTimeout)) {
                        /*
                        if (checkingHeightCPU == false) {
                            std::thread([&]() { checkBlockHeight(dynProgram); }).detach(); // WHISKERZ
                        }
                        */

                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                        // Sleep(1000);
                        time_t now;
                        time(&now);
                        if ((now - start) % 3 == 0) {
                            dynProgram->outputStats(dynProgram, now, start, globalNonceCount); // WHISKERZ
                            //  printf("hashrate: %8.2f\n", (float)globalNonceCount / (float)(now - start));
                        }
                        // if (now - start > 18) {
                        // globalTimeout = true;
                        // printf("Checking for stale block\n");
                        //}
                    }
                }

                if (toupper(dynProgram->minerType[0]) == 'G') {
                    // for (int i = 0; i < 1; i++) {
                    for (int i = 0; i < hashFunction->programs[0]->numOpenCLDevices; i++) {
                        std::thread t1(doGPUHash, i, header);
                        std::this_thread::sleep_for(std::chrono::milliseconds(strMerkleRoot[10]));
                        t1.detach();
                    }

                    time_t start;
                    time(&start);

                    while ((!globalFound) && (!globalTimeout)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                        // Sleep(1000);
                        // time_t now;
                        // time(&now);
                        // if ((now - start) % 3 == 0) {
                        // printf("Total hashrate: %8.2f\n", (float)globalNonceCount / (float)(now - start));
                        //}
                        // if (now - start > 18) {
                        // globalTimeout = true;
                        // printf("Checking for stale block\n");
                        //}
                    }
                }

                // printf("%d\n", _CrtCheckMemory());

                // submit solution

                // reverse previous hash byte order
                // prev hash is positions 4 to 36 in header
                for (int i = 0; i < 16; i++) {
                    unsigned char swap = header[4 + i];
                    header[4 + i] = header[35 - i];
                    header[35 - i] = swap;
                }

                std::string strBlock;

                char hexHeader[256];
                bin2hex(hexHeader, header, 80);
                strBlock += std::string(hexHeader);
                strBlock += transactionString;

                if (!globalTimeout) {
                    std::string postBlockRequest;
                    if (strcmp(mode, "pool") == 0)
                        postBlockRequest = "{ \"id\": 0, \"method\" : \"submitblock\", \"params\" : [\"" + strBlock
                                           + "\",\"" + poolShareWallet + "\"] }";
                    else
                        postBlockRequest = std::string("{ \"id\": 0, \"method\" : \"submitblock\", \"params\" : [\"")
                                           + strBlock + std::string("\"] }");

                    // printf("%s\n", postBlockRequest.c_str());

                    chunk.size = 0;

                    curl_easy_setopt(curl, CURLOPT_URL, dynProgram->strRPC_URL);
                    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
                    curl_easy_setopt(curl, CURLOPT_USERNAME, dynProgram->RPCUser);
                    curl_easy_setopt(curl, CURLOPT_PASSWORD, dynProgram->RPCPassword);

                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBlockRequest.c_str());

                    res = curl_easy_perform(curl);

                    if (res != CURLE_OK)
                        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                    else {
                        json result = json::parse(chunk.memory);

                        if (result["error"].is_null()) {
                            SetConsoleTextAttribute(hConsole, LIGHTCYAN);
                            printf(" **** SUBMITTED BLOCK SOLUTION FOR APPROVAL!!! ****\n");
                            SetConsoleTextAttribute(hConsole, LIGHTGRAY);
                            validateSubmission(dynProgram, dynProgram->height);
                        } else {
                            printf("Submit block failed.\n");
                        }
                    }
                }
            }
        }
    }
}

void diff_to_target(uint32_t* target, double diff) {
    uint64_t m;
    int k;

    for (k = 6; k > 0 && diff > 1.0; k--)
        diff /= 4294967296.0;
    m = 4294901760.0 / diff;
    if (m == 0 && k == 6)
        memset(target, 0xff, 32);
    else {
        memset(target, 0, 32);
        target[k] = (uint32_t)m;
        target[k + 1] = (uint32_t)(m >> 32);
    }
}

void memrev(unsigned char* p, size_t len) {
    unsigned char c, *q;
    for (q = p + len - 1; p < q; p++, q--) {
        c = *p;
        *p = *q;
        *q = c;
    }
}

void bin2hex(char* s, const unsigned char* p, size_t len) {
    int i;
    for (i = 0; i < len; i++)
#ifdef __linux__
        sprintf(s + (i * 2), "%02x", (unsigned int)p[i]);
#else
        sprintf_s(s + (i * 2), 3, "%02x", (unsigned int)p[i]);
#endif
}

bool hex2bin(unsigned char* p, const char* hexstr, size_t len) {
    if (hexstr == NULL) return false;

    size_t hexstr_len = strlen(hexstr);
    if ((hexstr_len % 2) != 0) {
        return false;
    }

    size_t bin_len = hexstr_len / 2;
    if (bin_len > len) {
        return false;
    }

    memset(p, 0, len);

    size_t i = 0;
    while (i < hexstr_len) {
        char c = hexstr[i];
        unsigned char nibble;
        if (c >= '0' && c <= '9') {
            nibble = (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (10 + (c - 'A'));
        } else if (c >= 'a' && c <= 'f') {
            nibble = (10 + (c - 'a'));
        } else {
            return false;
        }
        p[(i / 2)] |= (nibble << ((1 - (i % 2)) * 4));
        i++;
    }

    return true;
}

int varint_encode(unsigned char* p, uint64_t n) {
    int i;
    if (n < 0xfd) {
        p[0] = n;
        return 1;
    }
    if (n <= 0xffff) {
        p[0] = 0xfd;
        p[1] = n & 0xff;
        p[2] = n >> 8;
        return 3;
    }
    if (n <= 0xffffffff) {
        p[0] = 0xfe;
        for (i = 1; i < 5; i++) {
            p[i] = n & 0xff;
            n >>= 8;
        }
        return 5;
    }
    p[0] = 0xff;
    for (i = 1; i < 9; i++) {
        p[i] = n & 0xff;
        n >>= 8;
    }
    return 9;
}

static const char b58digits[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static bool b58dec(unsigned char* bin, size_t binsz, const char* b58) {
    size_t i, j;
    uint64_t t;
    uint32_t c;
    uint32_t* outi;
    size_t outisz = (binsz + 3) / 4;
    int rem = binsz % 4;
    uint32_t remmask = 0xffffffff << (8 * rem);
    size_t b58sz = strlen(b58);
    bool rc = false;

    outi = (uint32_t*)calloc(outisz, sizeof(*outi));

    for (i = 0; i < b58sz; ++i) {
        for (c = 0; b58digits[c] != b58[i]; c++)
            if (!b58digits[c]) goto out;
        for (j = outisz; j--;) {
            t = (uint64_t)outi[j] * 58 + c;
            c = t >> 32;
            outi[j] = t & 0xffffffff;
        }
        if (c || outi[0] & remmask) goto out;
    }

    j = 0;
    switch (rem) {
    case 3:
        *(bin++) = (outi[0] >> 16) & 0xff;
    case 2:
        *(bin++) = (outi[0] >> 8) & 0xff;
    case 1:
        *(bin++) = outi[0] & 0xff;
        ++j;
    default:
        break;
    }
    for (; j < outisz; ++j) {
        be32enc((uint32_t*)bin, outi[j]);
        bin += sizeof(uint32_t);
    }

    rc = true;
out:
    free(outi);
    return rc;
}

static int b58check(unsigned char* bin, size_t binsz, const char* b58) {
    unsigned char buf[32];
    int i;

    sha256d(buf, bin, binsz - 4);
    if (memcmp(&bin[binsz - 4], buf, 4)) return -1;

    /* Check number of zeros is correct AFTER verifying checksum
     * (to avoid possibility of accessing the string beyond the end) */
    for (i = 0; bin[i] == '\0' && b58[i] == '1'; ++i)
        ;
    if (bin[i] == '\0' || b58[i] == '1') return -3;

    return bin[0];
}

static uint32_t bech32_polymod_step(uint32_t pre) {
    uint8_t b = pre >> 25;
    return ((pre & 0x1FFFFFF) << 5) ^ (-((b >> 0) & 1) & 0x3b6a57b2UL) ^ (-((b >> 1) & 1) & 0x26508e6dUL)
           ^ (-((b >> 2) & 1) & 0x1ea119faUL) ^ (-((b >> 3) & 1) & 0x3d4233ddUL) ^ (-((b >> 4) & 1) & 0x2a1462b3UL);
}

static const int8_t bech32_charset_rev[128] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 15, -1, 10, 17,
  21, 20, 26, 30, 7,  5,  -1, -1, -1, -1, -1, -1, -1, 29, -1, 24, 13, 25, 9,  8,  23, -1, 18, 22, 31, 27,
  19, -1, 1,  0,  3,  16, 11, 28, 12, 14, 6,  4,  2,  -1, -1, -1, -1, -1, -1, 29, -1, 24, 13, 25, 9,  8,
  23, -1, 18, 22, 31, 27, 19, -1, 1,  0,  3,  16, 11, 28, 12, 14, 6,  4,  2,  -1, -1, -1, -1, -1};

static bool bech32_decode(char* hrp, uint8_t* data, size_t* data_len, const char* input) {
    uint32_t chk = 1;
    size_t i;
    size_t input_len = strlen(input);
    size_t hrp_len;
    int have_lower = 0, have_upper = 0;
    if (input_len < 8 || input_len > 90) {
        return false;
    }
    *data_len = 0;
    while (*data_len < input_len && input[(input_len - 1) - *data_len] != '1') {
        ++(*data_len);
    }
    hrp_len = input_len - (1 + *data_len);
    if (1 + *data_len >= input_len || *data_len < 6) {
        return false;
    }
    *(data_len) -= 6;
    for (i = 0; i < hrp_len; ++i) {
        int ch = input[i];
        if (ch < 33 || ch > 126) {
            return false;
        }
        if (ch >= 'a' && ch <= 'z') {
            have_lower = 1;
        } else if (ch >= 'A' && ch <= 'Z') {
            have_upper = 1;
            ch = (ch - 'A') + 'a';
        }
        hrp[i] = ch;
        chk = bech32_polymod_step(chk) ^ (ch >> 5);
    }
    hrp[i] = 0;
    chk = bech32_polymod_step(chk);
    for (i = 0; i < hrp_len; ++i) {
        chk = bech32_polymod_step(chk) ^ (input[i] & 0x1f);
    }
    ++i;
    while (i < input_len) {
        int v = (input[i] & 0x80) ? -1 : bech32_charset_rev[(int)input[i]];
        if (input[i] >= 'a' && input[i] <= 'z') have_lower = 1;
        if (input[i] >= 'A' && input[i] <= 'Z') have_upper = 1;
        if (v == -1) {
            return false;
        }
        chk = bech32_polymod_step(chk) ^ v;
        if (i + 6 < input_len) {
            data[i - (1 + hrp_len)] = v;
        }
        ++i;
    }
    if (have_lower && have_upper) {
        return false;
    }
    return chk == 1;
}

static bool
convert_bits(uint8_t* out, size_t* outlen, int outbits, const uint8_t* in, size_t inlen, int inbits, int pad) {
    uint32_t val = 0;
    int bits = 0;
    uint32_t maxv = (((uint32_t)1) << outbits) - 1;
    while (inlen--) {
        val = (val << inbits) | *(in++);
        bits += inbits;
        while (bits >= outbits) {
            bits -= outbits;
            out[(*outlen)++] = (val >> bits) & maxv;
        }
    }
    if (pad) {
        if (bits) {
            out[(*outlen)++] = (val << (outbits - bits)) & maxv;
        }
    } else if (((val << (outbits - bits)) & maxv) || bits >= inbits) {
        return false;
    }
    return true;
}

static bool segwit_addr_decode(int* witver, uint8_t* witdata, size_t* witdata_len, const char* addr) {
    uint8_t data[84];
    char hrp_actual[84];
    size_t data_len;
    if (!bech32_decode(hrp_actual, data, &data_len, addr)) return false;
    if (data_len == 0 || data_len > 65) return false;
    if (data[0] > 16) return false;
    *witdata_len = 0;
    if (!convert_bits(witdata, witdata_len, 8, data + 1, data_len - 1, 5, 0)) return false;
    if (*witdata_len < 2 || *witdata_len > 40) return false;
    if (data[0] == 0 && *witdata_len != 20 && *witdata_len != 32) return false;
    *witver = data[0];
    return true;
}

static size_t bech32_to_script(uint8_t* out, size_t outsz, const char* addr) {
    uint8_t witprog[40];
    size_t witprog_len;
    int witver;

    if (!segwit_addr_decode(&witver, witprog, &witprog_len, addr)) return 0;
    if (outsz < witprog_len + 2) return 0;
    out[0] = witver ? (0x50 + witver) : 0;
    out[1] = witprog_len;
    memcpy(out + 2, witprog, witprog_len);
    return witprog_len + 2;
}

size_t address_to_script(unsigned char* out, size_t outsz, const char* addr) {
    unsigned char addrbin[25];
    int addrver;
    size_t rv;

    if (!b58dec(addrbin, sizeof(addrbin), addr)) return bech32_to_script(out, outsz, addr);
    addrver = b58check(addrbin, sizeof(addrbin), addr);
    if (addrver < 0) return 0;
    switch (addrver) {
    case 5:   /* Bitcoin script hash */
    case 196: /* Testnet script hash */
        if (outsz < (rv = 23)) return rv;
        out[0] = 0xa9; /* OP_HASH160 */
        out[1] = 0x14; /* push 20 bytes */
        memcpy(&out[2], &addrbin[1], 20);
        out[22] = 0x87; /* OP_EQUAL */
        return rv;
    default:
        if (outsz < (rv = 25)) return rv;
        out[0] = 0x76; /* OP_DUP */
        out[1] = 0xa9; /* OP_HASH160 */
        out[2] = 0x14; /* push 20 bytes */
        memcpy(&out[3], &addrbin[1], 20);
        out[23] = 0x88; /* OP_EQUALVERIFY */
        out[24] = 0xac; /* OP_CHECKSIG */
        return rv;
    }
}

////=======================================================

/*

using json = nlohmann::json;

json j = "{ \"id\": 0, \"method\" : \"createwallet\", \"params\" : [\"test\"] }"_json;
std::string jData = j.dump();

struct MemoryStruct chunk;

chunk.memory = (char*)malloc(1);
chunk.size = 0;

CURL* curl;
CURLcode res;

curl_global_init(CURL_GLOBAL_ALL);

curl = curl_easy_init();
if (curl) {
    //curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.62:6433");
    curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.62:6433");
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, "user");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "123456");

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jData.c_str());

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    else {
        json result = json::parse(chunk.memory);

    }

    curl_easy_cleanup(curl);
}

free(chunk.memory);


curl_global_cleanup();


*/

/*
using json = nlohmann::json;

json j = "{ \"id\": 0, \"method\" : \"getnewaddress\", \"params\" : [] }"_json;
std::string jData = j.dump();

struct MemoryStruct chunk;

chunk.memory = (char*)malloc(1);
chunk.size = 0;

CURL* curl;
CURLcode res;

curl_global_init(CURL_GLOBAL_ALL);

curl = curl_easy_init();
if (curl) {
    //curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.62:6433");
    curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.62:6433");
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, "user");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "123456");

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jData.c_str());

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    else {
        json result = json::parse(chunk.memory);
        printf ("%s", result.dump().c_str());

    }

    curl_easy_cleanup(curl);
}

free(chunk.memory);


curl_global_cleanup();

*/

/*
using json = nlohmann::json;

json j = "{ \"id\": 0, \"method\" : \"generatetoaddress\", \"params\" : [1,
\"dy1q6y6uv9thwl99up2l4pj9q3l4lfuwml6wn5863q\"] }"_json; std::string jData = j.dump();

struct MemoryStruct chunk;

chunk.memory = (char*)malloc(1);
chunk.size = 0;

CURL* curl;
CURLcode res;

curl_global_init(CURL_GLOBAL_ALL);

curl = curl_easy_init();
if (curl) {
    //curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.62:6433");
    curl_easy_setopt(curl, CURLOPT_URL, "http://192.168.1.62:6433");
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, "user");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "123456");

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jData.c_str());

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    else {
        json result = json::parse(chunk.memory);
        printf("%s", result.dump().c_str());
    }

    curl_easy_cleanup(curl);
}

free(chunk.memory);


curl_global_cleanup();





}
*/

#if defined(USE_ASM)                                                                                                   \
  && (defined(__x86_64__) || (defined(__arm__) && defined(__APCS_32__)) || (defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)))
#define EXTERN_SHA256
#endif

static const uint32_t sha256_h[8] = {
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

static const uint32_t sha256_k[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void sha256_init(uint32_t* state) { memcpy(state, sha256_h, 32); }

/* Elementary functions used by SHA256 */
#define Ch(x, y, z) ((x & (y ^ z)) ^ z)
#define Maj(x, y, z) ((x & (y | z)) | (y & z))
#define ROTR(x, n) ((x >> n) | (x << (32 - n)))
#define S0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ (x >> 3))
#define s1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ (x >> 10))

/* SHA256 round function */
#define RND(a, b, c, d, e, f, g, h, k)                                                                                 \
    do {                                                                                                               \
        t0 = h + S1(e) + Ch(e, f, g) + k;                                                                              \
        t1 = S0(a) + Maj(a, b, c);                                                                                     \
        d += t0;                                                                                                       \
        h = t0 + t1;                                                                                                   \
    } while (0)

/* Adjusted round function for rotating state */
#define RNDr(S, W, i)                                                                                                  \
    RND(                                                                                                               \
      S[(64 - i) % 8],                                                                                                 \
      S[(65 - i) % 8],                                                                                                 \
      S[(66 - i) % 8],                                                                                                 \
      S[(67 - i) % 8],                                                                                                 \
      S[(68 - i) % 8],                                                                                                 \
      S[(69 - i) % 8],                                                                                                 \
      S[(70 - i) % 8],                                                                                                 \
      S[(71 - i) % 8],                                                                                                 \
      W[i] + sha256_k[i])

#ifndef EXTERN_SHA256

/*
 * SHA256 block compression function.  The 256-bit state is transformed via
 * the 512-bit input block to produce a new state.
 */
void sha256_transform(uint32_t* state, const uint32_t* block, int swap) {
    uint32_t W[64];
    uint32_t S[8];
    uint32_t t0, t1;
    int i;

    /* 1. Prepare message schedule W. */
    if (swap) {
        for (i = 0; i < 16; i++)
            W[i] = swab32(block[i]);
    } else
        memcpy(W, block, 64);
    for (i = 16; i < 64; i += 2) {
        W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];
        W[i + 1] = s1(W[i - 1]) + W[i - 6] + s0(W[i - 14]) + W[i - 15];
    }

    /* 2. Initialize working variables. */
    memcpy(S, state, 32);

    /* 3. Mix. */
    RNDr(S, W, 0);
    RNDr(S, W, 1);
    RNDr(S, W, 2);
    RNDr(S, W, 3);
    RNDr(S, W, 4);
    RNDr(S, W, 5);
    RNDr(S, W, 6);
    RNDr(S, W, 7);
    RNDr(S, W, 8);
    RNDr(S, W, 9);
    RNDr(S, W, 10);
    RNDr(S, W, 11);
    RNDr(S, W, 12);
    RNDr(S, W, 13);
    RNDr(S, W, 14);
    RNDr(S, W, 15);
    RNDr(S, W, 16);
    RNDr(S, W, 17);
    RNDr(S, W, 18);
    RNDr(S, W, 19);
    RNDr(S, W, 20);
    RNDr(S, W, 21);
    RNDr(S, W, 22);
    RNDr(S, W, 23);
    RNDr(S, W, 24);
    RNDr(S, W, 25);
    RNDr(S, W, 26);
    RNDr(S, W, 27);
    RNDr(S, W, 28);
    RNDr(S, W, 29);
    RNDr(S, W, 30);
    RNDr(S, W, 31);
    RNDr(S, W, 32);
    RNDr(S, W, 33);
    RNDr(S, W, 34);
    RNDr(S, W, 35);
    RNDr(S, W, 36);
    RNDr(S, W, 37);
    RNDr(S, W, 38);
    RNDr(S, W, 39);
    RNDr(S, W, 40);
    RNDr(S, W, 41);
    RNDr(S, W, 42);
    RNDr(S, W, 43);
    RNDr(S, W, 44);
    RNDr(S, W, 45);
    RNDr(S, W, 46);
    RNDr(S, W, 47);
    RNDr(S, W, 48);
    RNDr(S, W, 49);
    RNDr(S, W, 50);
    RNDr(S, W, 51);
    RNDr(S, W, 52);
    RNDr(S, W, 53);
    RNDr(S, W, 54);
    RNDr(S, W, 55);
    RNDr(S, W, 56);
    RNDr(S, W, 57);
    RNDr(S, W, 58);
    RNDr(S, W, 59);
    RNDr(S, W, 60);
    RNDr(S, W, 61);
    RNDr(S, W, 62);
    RNDr(S, W, 63);

    /* 4. Mix local working variables into global state */
    for (i = 0; i < 8; i++)
        state[i] += S[i];
}

#endif /* EXTERN_SHA256 */

static const uint32_t sha256d_hash1[16] = {
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x80000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000000,
  0x00000100};

static void sha256d_80_swap(uint32_t* hash, const uint32_t* data) {
    uint32_t S[16];
    int i;

    sha256_init(S);
    sha256_transform(S, data, 0);
    sha256_transform(S, data + 16, 0);
    memcpy(S + 8, sha256d_hash1 + 8, 32);
    sha256_init(hash);
    sha256_transform(hash, S, 0);
    for (i = 0; i < 8; i++)
        hash[i] = swab32(hash[i]);
}

void sha256d(unsigned char* hash, const unsigned char* data, int len) {
    uint32_t S[16], T[16];
    int i, r;

    sha256_init(S);
    for (r = len; r > -9; r -= 64) {
        if (r < 64) memset(T, 0, 64);
        memcpy(T, data + len - r, r > 64 ? 64 : (r < 0 ? 0 : r));
        if (r >= 0 && r < 64) ((unsigned char*)T)[r] = 0x80;
        for (i = 0; i < 16; i++)
            T[i] = be32dec(T + i);
        if (r < 56) T[15] = 8 * len;
        sha256_transform(S, T, 0);
    }
    memcpy(S + 8, sha256d_hash1 + 8, 32);
    sha256_init(T);
    sha256_transform(T, S, 0);
    for (i = 0; i < 8; i++)
        be32enc((uint32_t*)hash + i, T[i]);
}

static inline void sha256d_preextend(uint32_t* W) {
    W[16] = s1(W[14]) + W[9] + s0(W[1]) + W[0];
    W[17] = s1(W[15]) + W[10] + s0(W[2]) + W[1];
    W[18] = s1(W[16]) + W[11] + W[2];
    W[19] = s1(W[17]) + W[12] + s0(W[4]);
    W[20] = W[13] + s0(W[5]) + W[4];
    W[21] = W[14] + s0(W[6]) + W[5];
    W[22] = W[15] + s0(W[7]) + W[6];
    W[23] = W[16] + s0(W[8]) + W[7];
    W[24] = W[17] + s0(W[9]) + W[8];
    W[25] = s0(W[10]) + W[9];
    W[26] = s0(W[11]) + W[10];
    W[27] = s0(W[12]) + W[11];
    W[28] = s0(W[13]) + W[12];
    W[29] = s0(W[14]) + W[13];
    W[30] = s0(W[15]) + W[14];
    W[31] = s0(W[16]) + W[15];
}

static inline void sha256d_prehash(uint32_t* S, const uint32_t* W) {
    uint32_t t0, t1;
    RNDr(S, W, 0);
    RNDr(S, W, 1);
    RNDr(S, W, 2);
}

#ifdef EXTERN_SHA256

void sha256d_ms(uint32_t* hash, uint32_t* W, const uint32_t* midstate, const uint32_t* prehash);

#else

static inline void sha256d_ms(uint32_t* hash, uint32_t* W, const uint32_t* midstate, const uint32_t* prehash) {
    uint32_t S[64];
    uint32_t t0, t1;
    int i;

    S[18] = W[18];
    S[19] = W[19];
    S[20] = W[20];
    S[22] = W[22];
    S[23] = W[23];
    S[24] = W[24];
    S[30] = W[30];
    S[31] = W[31];

    W[18] += s0(W[3]);
    W[19] += W[3];
    W[20] += s1(W[18]);
    W[21] = s1(W[19]);
    W[22] += s1(W[20]);
    W[23] += s1(W[21]);
    W[24] += s1(W[22]);
    W[25] = s1(W[23]) + W[18];
    W[26] = s1(W[24]) + W[19];
    W[27] = s1(W[25]) + W[20];
    W[28] = s1(W[26]) + W[21];
    W[29] = s1(W[27]) + W[22];
    W[30] += s1(W[28]) + W[23];
    W[31] += s1(W[29]) + W[24];
    for (i = 32; i < 64; i += 2) {
        W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];
        W[i + 1] = s1(W[i - 1]) + W[i - 6] + s0(W[i - 14]) + W[i - 15];
    }

    memcpy(S, prehash, 32);

    RNDr(S, W, 3);
    RNDr(S, W, 4);
    RNDr(S, W, 5);
    RNDr(S, W, 6);
    RNDr(S, W, 7);
    RNDr(S, W, 8);
    RNDr(S, W, 9);
    RNDr(S, W, 10);
    RNDr(S, W, 11);
    RNDr(S, W, 12);
    RNDr(S, W, 13);
    RNDr(S, W, 14);
    RNDr(S, W, 15);
    RNDr(S, W, 16);
    RNDr(S, W, 17);
    RNDr(S, W, 18);
    RNDr(S, W, 19);
    RNDr(S, W, 20);
    RNDr(S, W, 21);
    RNDr(S, W, 22);
    RNDr(S, W, 23);
    RNDr(S, W, 24);
    RNDr(S, W, 25);
    RNDr(S, W, 26);
    RNDr(S, W, 27);
    RNDr(S, W, 28);
    RNDr(S, W, 29);
    RNDr(S, W, 30);
    RNDr(S, W, 31);
    RNDr(S, W, 32);
    RNDr(S, W, 33);
    RNDr(S, W, 34);
    RNDr(S, W, 35);
    RNDr(S, W, 36);
    RNDr(S, W, 37);
    RNDr(S, W, 38);
    RNDr(S, W, 39);
    RNDr(S, W, 40);
    RNDr(S, W, 41);
    RNDr(S, W, 42);
    RNDr(S, W, 43);
    RNDr(S, W, 44);
    RNDr(S, W, 45);
    RNDr(S, W, 46);
    RNDr(S, W, 47);
    RNDr(S, W, 48);
    RNDr(S, W, 49);
    RNDr(S, W, 50);
    RNDr(S, W, 51);
    RNDr(S, W, 52);
    RNDr(S, W, 53);
    RNDr(S, W, 54);
    RNDr(S, W, 55);
    RNDr(S, W, 56);
    RNDr(S, W, 57);
    RNDr(S, W, 58);
    RNDr(S, W, 59);
    RNDr(S, W, 60);
    RNDr(S, W, 61);
    RNDr(S, W, 62);
    RNDr(S, W, 63);

    for (i = 0; i < 8; i++)
        S[i] += midstate[i];

    W[18] = S[18];
    W[19] = S[19];
    W[20] = S[20];
    W[22] = S[22];
    W[23] = S[23];
    W[24] = S[24];
    W[30] = S[30];
    W[31] = S[31];

    memcpy(S + 8, sha256d_hash1 + 8, 32);
    S[16] = s1(sha256d_hash1[14]) + sha256d_hash1[9] + s0(S[1]) + S[0];
    S[17] = s1(sha256d_hash1[15]) + sha256d_hash1[10] + s0(S[2]) + S[1];
    S[18] = s1(S[16]) + sha256d_hash1[11] + s0(S[3]) + S[2];
    S[19] = s1(S[17]) + sha256d_hash1[12] + s0(S[4]) + S[3];
    S[20] = s1(S[18]) + sha256d_hash1[13] + s0(S[5]) + S[4];
    S[21] = s1(S[19]) + sha256d_hash1[14] + s0(S[6]) + S[5];
    S[22] = s1(S[20]) + sha256d_hash1[15] + s0(S[7]) + S[6];
    S[23] = s1(S[21]) + S[16] + s0(sha256d_hash1[8]) + S[7];
    S[24] = s1(S[22]) + S[17] + s0(sha256d_hash1[9]) + sha256d_hash1[8];
    S[25] = s1(S[23]) + S[18] + s0(sha256d_hash1[10]) + sha256d_hash1[9];
    S[26] = s1(S[24]) + S[19] + s0(sha256d_hash1[11]) + sha256d_hash1[10];
    S[27] = s1(S[25]) + S[20] + s0(sha256d_hash1[12]) + sha256d_hash1[11];
    S[28] = s1(S[26]) + S[21] + s0(sha256d_hash1[13]) + sha256d_hash1[12];
    S[29] = s1(S[27]) + S[22] + s0(sha256d_hash1[14]) + sha256d_hash1[13];
    S[30] = s1(S[28]) + S[23] + s0(sha256d_hash1[15]) + sha256d_hash1[14];
    S[31] = s1(S[29]) + S[24] + s0(S[16]) + sha256d_hash1[15];
    for (i = 32; i < 60; i += 2) {
        S[i] = s1(S[i - 2]) + S[i - 7] + s0(S[i - 15]) + S[i - 16];
        S[i + 1] = s1(S[i - 1]) + S[i - 6] + s0(S[i - 14]) + S[i - 15];
    }
    S[60] = s1(S[58]) + S[53] + s0(S[45]) + S[44];

    sha256_init(hash);

    RNDr(hash, S, 0);
    RNDr(hash, S, 1);
    RNDr(hash, S, 2);
    RNDr(hash, S, 3);
    RNDr(hash, S, 4);
    RNDr(hash, S, 5);
    RNDr(hash, S, 6);
    RNDr(hash, S, 7);
    RNDr(hash, S, 8);
    RNDr(hash, S, 9);
    RNDr(hash, S, 10);
    RNDr(hash, S, 11);
    RNDr(hash, S, 12);
    RNDr(hash, S, 13);
    RNDr(hash, S, 14);
    RNDr(hash, S, 15);
    RNDr(hash, S, 16);
    RNDr(hash, S, 17);
    RNDr(hash, S, 18);
    RNDr(hash, S, 19);
    RNDr(hash, S, 20);
    RNDr(hash, S, 21);
    RNDr(hash, S, 22);
    RNDr(hash, S, 23);
    RNDr(hash, S, 24);
    RNDr(hash, S, 25);
    RNDr(hash, S, 26);
    RNDr(hash, S, 27);
    RNDr(hash, S, 28);
    RNDr(hash, S, 29);
    RNDr(hash, S, 30);
    RNDr(hash, S, 31);
    RNDr(hash, S, 32);
    RNDr(hash, S, 33);
    RNDr(hash, S, 34);
    RNDr(hash, S, 35);
    RNDr(hash, S, 36);
    RNDr(hash, S, 37);
    RNDr(hash, S, 38);
    RNDr(hash, S, 39);
    RNDr(hash, S, 40);
    RNDr(hash, S, 41);
    RNDr(hash, S, 42);
    RNDr(hash, S, 43);
    RNDr(hash, S, 44);
    RNDr(hash, S, 45);
    RNDr(hash, S, 46);
    RNDr(hash, S, 47);
    RNDr(hash, S, 48);
    RNDr(hash, S, 49);
    RNDr(hash, S, 50);
    RNDr(hash, S, 51);
    RNDr(hash, S, 52);
    RNDr(hash, S, 53);
    RNDr(hash, S, 54);
    RNDr(hash, S, 55);
    RNDr(hash, S, 56);

    hash[2] += hash[6] + S1(hash[3]) + Ch(hash[3], hash[4], hash[5]) + S[57] + sha256_k[57];
    hash[1] += hash[5] + S1(hash[2]) + Ch(hash[2], hash[3], hash[4]) + S[58] + sha256_k[58];
    hash[0] += hash[4] + S1(hash[1]) + Ch(hash[1], hash[2], hash[3]) + S[59] + sha256_k[59];
    hash[7] += hash[3] + S1(hash[0]) + Ch(hash[0], hash[1], hash[2]) + S[60] + sha256_k[60] + sha256_h[7];
}

#endif /* EXTERN_SHA256 */

#ifdef HAVE_SHA256_4WAY

void sha256d_ms_4way(uint32_t* hash, uint32_t* data, const uint32_t* midstate, const uint32_t* prehash);

static inline int scanhash_sha256d_4way(
  int thr_id, uint32_t* pdata, const uint32_t* ptarget, uint32_t max_nonce, unsigned long* hashes_done) {
    uint32_t data[4 * 64] __attribute__((aligned(128)));
    uint32_t hash[4 * 8] __attribute__((aligned(32)));
    uint32_t midstate[4 * 8] __attribute__((aligned(32)));
    uint32_t prehash[4 * 8] __attribute__((aligned(32)));
    uint32_t n = pdata[19] - 1;
    const uint32_t first_nonce = pdata[19];
    const uint32_t Htarg = ptarget[7];
    int i, j;

    memcpy(data, pdata + 16, 64);
    sha256d_preextend(data);
    for (i = 31; i >= 0; i--)
        for (j = 0; j < 4; j++)
            data[i * 4 + j] = data[i];

    sha256_init(midstate);
    sha256_transform(midstate, pdata, 0);
    memcpy(prehash, midstate, 32);
    sha256d_prehash(prehash, pdata + 16);
    for (i = 7; i >= 0; i--) {
        for (j = 0; j < 4; j++) {
            midstate[i * 4 + j] = midstate[i];
            prehash[i * 4 + j] = prehash[i];
        }
    }

    do {
        for (i = 0; i < 4; i++)
            data[4 * 3 + i] = ++n;

        sha256d_ms_4way(hash, data, midstate, prehash);

        for (i = 0; i < 4; i++) {
            if (swab32(hash[4 * 7 + i]) <= Htarg) {
                pdata[19] = data[4 * 3 + i];
                sha256d_80_swap(hash, pdata);
                if (fulltest(hash, ptarget)) {
                    *hashes_done = n - first_nonce + 1;
                    return 1;
                }
            }
        }
    } while (n < max_nonce && !work_restart[thr_id].restart);

    *hashes_done = n - first_nonce + 1;
    pdata[19] = n;
    return 0;
}

#endif /* HAVE_SHA256_4WAY */

#ifdef HAVE_SHA256_8WAY

void sha256d_ms_8way(uint32_t* hash, uint32_t* data, const uint32_t* midstate, const uint32_t* prehash);

static inline int scanhash_sha256d_8way(
  int thr_id, uint32_t* pdata, const uint32_t* ptarget, uint32_t max_nonce, unsigned long* hashes_done) {
    uint32_t data[8 * 64] __attribute__((aligned(128)));
    uint32_t hash[8 * 8] __attribute__((aligned(32)));
    uint32_t midstate[8 * 8] __attribute__((aligned(32)));
    uint32_t prehash[8 * 8] __attribute__((aligned(32)));
    uint32_t n = pdata[19] - 1;
    const uint32_t first_nonce = pdata[19];
    const uint32_t Htarg = ptarget[7];
    int i, j;

    memcpy(data, pdata + 16, 64);
    sha256d_preextend(data);
    for (i = 31; i >= 0; i--)
        for (j = 0; j < 8; j++)
            data[i * 8 + j] = data[i];

    sha256_init(midstate);
    sha256_transform(midstate, pdata, 0);
    memcpy(prehash, midstate, 32);
    sha256d_prehash(prehash, pdata + 16);
    for (i = 7; i >= 0; i--) {
        for (j = 0; j < 8; j++) {
            midstate[i * 8 + j] = midstate[i];
            prehash[i * 8 + j] = prehash[i];
        }
    }

    do {
        for (i = 0; i < 8; i++)
            data[8 * 3 + i] = ++n;

        sha256d_ms_8way(hash, data, midstate, prehash);

        for (i = 0; i < 8; i++) {
            if (swab32(hash[8 * 7 + i]) <= Htarg) {
                pdata[19] = data[8 * 3 + i];
                sha256d_80_swap(hash, pdata);
                if (fulltest(hash, ptarget)) {
                    *hashes_done = n - first_nonce + 1;
                    return 1;
                }
            }
        }
    } while (n < max_nonce && !work_restart[thr_id].restart);

    *hashes_done = n - first_nonce + 1;
    pdata[19] = n;
    return 0;
}

#endif /* HAVE_SHA256_8WAY */

/*
int scanhash_sha256d(int thr_id, uint32_t *pdata, const uint32_t *ptarget,
    uint32_t max_nonce, unsigned long *hashes_done)
{
    uint32_t data[64] __attribute__((aligned(128)));
    uint32_t hash[8] __attribute__((aligned(32)));
    uint32_t midstate[8] __attribute__((aligned(32)));
    uint32_t prehash[8] __attribute__((aligned(32)));
    uint32_t n = pdata[19] - 1;
    const uint32_t first_nonce = pdata[19];
    const uint32_t Htarg = ptarget[7];

#ifdef HAVE_SHA256_8WAY
    if (sha256_use_8way())
        return scanhash_sha256d_8way(thr_id, pdata, ptarget,
            max_nonce, hashes_done);
#endif
#ifdef HAVE_SHA256_4WAY
    if (sha256_use_4way())
        return scanhash_sha256d_4way(thr_id, pdata, ptarget,
            max_nonce, hashes_done);
#endif

    memcpy(data, pdata + 16, 64);
    sha256d_preextend(data);

    sha256_init(midstate);
    sha256_transform(midstate, pdata, 0);
    memcpy(prehash, midstate, 32);
    sha256d_prehash(prehash, pdata + 16);

    do {
        data[3] = ++n;
        sha256d_ms(hash, data, midstate, prehash);
        if (swab32(hash[7]) <= Htarg) {
            pdata[19] = data[3];
            sha256d_80_swap(hash, pdata);
            if (fulltest(hash, ptarget)) {
                *hashes_done = n - first_nonce + 1;
                return 1;
            }
        }
    } while (n < max_nonce && !work_restart[thr_id].restart);

    *hashes_done = n - first_nonce + 1;
    pdata[19] = n;
    return 0;
}

*/

/*
// WHISKERZ CODE

bool checkBlockHeight(CDynProgram* dynProgram)
{
    checkingHeightCPU = true;
    CURL* curl;
    CURLcode res;


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
        //printf("%s\n", result.dump().c_str());

        //printf("Chain Block is at: %" PRIu32 "\n", chainHeight);
        if (dynProgram->height != chainHeight) {
            printf("Block %d has gone stale, switching to current block %d\n", dynProgram->height, chainHeight);
            globalTimeout = true;
        }
    }

    Sleep(1000);
    checkingHeightCPU = false;
    return(true);
}

bool validateSubmission(CDynProgram* dynProgram, uint32_t block) {
    CURL* curl;
    CURLcode res;

    nlohmann::json j = { {"id",0}, {"method","getblockhash"}, {"params",{block}} };
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
        std::string blockhash = result["result"].dump().c_str();
        blockhash.erase(remove(blockhash.begin(), blockhash.end(), '\"'), blockhash.end());

        nlohmann::json j = { {"id",0}, {"method","getblock"}, {"params",{blockhash}} };
        jData = j.dump();
        chunk.size = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jData.c_str());
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        else {
            nlohmann::json result = nlohmann::json::parse(chunk.memory);

            //sprintf(blockhash, result.dump().c_str());
            std::string tx = result["result"]["tx"][0].dump().c_str();
            tx.erase(remove(tx.begin(), tx.end(), '\"'), tx.end());

            nlohmann::json j = { {"id",0}, {"method","getrawtransaction"}, {"params", {tx, true} } };
            jData = j.dump();
            chunk.size = 0;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jData.c_str());
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            else {
                nlohmann::json result = nlohmann::json::parse(chunk.memory);
                //printf("%s", result.dump().c_str());
                std::string address = result["result"]["vout"][0]["scriptPubKey"]["address"].dump().c_str();
                address.erase(remove(address.begin(), address.end(), '\"'), address.end());
                //printf("ADDRESS: '%s' vs '%s'\n", address.c_str(), dynProgram->minerPayToAddr.c_str());

                if (address != dynProgram->minerPayToAddr) {
                    dynProgram->rejectedBlocks++;
                    printf("  Submission failed, '%s' beat you to the draw! Pew pew!\n", address.c_str());
                }
                else {
                    dynProgram->acceptedBlocks++;
                    printf("  Submission SUCCESSFUL!! You p3wnd that block!! Sent to '%s'\n",
dynProgram->minerPayToAddr.c_str());
                }

            }
        }

    }

    return(true);
}
// END WHISKERZ
*/
