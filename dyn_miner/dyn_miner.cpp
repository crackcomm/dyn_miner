// dyn_miner.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "dyn_stratum.h"
#include "dynprogram.h"
#include "nlohmann/json.hpp"
#include "sha256.h"
#include "util/common.h"
#include "util/hex.h"
#include "util/sockets.h"
#include "util/stats.h"

#ifdef GPU_MINER
#include "dyn_miner_gpu.h"
#endif

#ifdef __linux__
#include <linux/kernel.h> /* for struct sysinfo */
#include <linux/unistd.h> /* for _syscallX macros/related stuff */
#include <sys/signal.h>
#include <sys/sysinfo.h>
//#include <sched.h>
#endif

#include <thread>

#ifdef DEBUG_LOGS
#define DEBUG_LOG(F, ARGS...) printf(F, ARGS)
#else
#define DEBUG_LOG(F, ARGS...)
#endif

using json = nlohmann::json;

enum class miner_device {
    CPU,
    GPU,
};

struct dyn_miner {
#ifdef GPU_MINER
    CDynProgramGPU gpu_program{};
#endif
    shared_work_t shared_work{};
    shares_t shares{};

    int compute_units{};
    int gpu_platform_id{};

    dyn_miner() = default;

    void start_cpu(uint32_t);
    void start_gpu(uint32_t gpuIndex);
    void set_job(const json& msg, miner_device device);
    void wait_for_work();

    inline void set_difficulty(double diff) {
        shared_work.set_difficulty(diff);
        shares.latest_diff = static_cast<uint32_t>(diff);
    }
};

uint32_t rand_nonce() {
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

void dyn_miner::wait_for_work() {
    while (shared_work.num.load(std::memory_order_relaxed) == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

#ifdef GPU_MINER
void dyn_miner::start_gpu(uint32_t gpuIndex) {
    wait_for_work();
    while (true) {
        gpu_program.startMiner(shared_work, compute_units, gpuIndex, shares);
    }
}
#endif

void cpu_miner(shared_work_t& shared_work, shares_t& shares, uint32_t index) {
    work_t work = shared_work.clone();
    uint32_t nonce = (shares.nonce_count.load(std::memory_order_relaxed) + rand_nonce()) * (index + 1);

    unsigned char header[80];
    memcpy(header, work.native_data, 80);
    memcpy(header + 76, &nonce, 4);

    unsigned char result[32];
    while (shared_work == work) {
        execute_program(result, header, work.program, work.prev_block_hash, work.merkle_root);
        shares.nonce_count++;

        uint64_t hash_int = *(uint64_t*)&result[24];
        if (hash_int <= work.share_target) {
            const share_t share = work.share((char*)header + 76);
            shares.append(share);
            return;
        }

        nonce++;
        memcpy(header + 76, &nonce, 4);
    }
}

void dyn_miner::start_cpu(uint32_t index) {
    /*
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(index, &set);
    if (sched_setaffinity(getpid(), sizeof(set), &set) < 0) printf("sched_setaffinity failed\n");
#endif
    */
    wait_for_work();
    while (true) {
        cpu_miner(shared_work, shares, index);
    }
}

void dyn_miner::set_job(const json& msg, miner_device device) {
    std::unique_lock<std::shared_mutex> _lock(shared_work.mutex);
    const std::vector<json>& params = msg["params"];

    work_t& work = shared_work.work;
    work.job_id = params[0];                            // job->id
    const std::string& hex_prev_block_hash = params[1]; // templ->prevhash_be
    hex2bin((unsigned char*)(work.prev_block_hash), hex_prev_block_hash.c_str(), 32);

    const std::string& coinb1 = params[2]; // templ->coinb1
    const std::string& coinb2 = params[3]; // templ->coinb2
    const std::string& nbits = params[6];  // templ->nbits
    work.hex_ntime = params[7];            // templ->ntime

    // set work program
    const std::string& program = params[8];
    [[maybe_unused]] const bool init_gpu = work.set_program(program);
#ifdef GPU_MINER
    if (device == miner_device::GPU && (init_gpu || gpu_program.kernel.kernel == NULL)) {
        gpu_program.kernel.initOpenCL(gpu_platform_id, compute_units, work.program);
    }
#endif

    unsigned char coinbase[4 * 1024] = {0};
    hex2bin(coinbase, coinb1.c_str(), coinb1.size());
    hex2bin(coinbase + (coinb1.size() / 2), coinb2.c_str(), coinb2.size());
    size_t coinbase_size = (coinb1.size() + coinb2.size()) / 2;

    uint32_t ntime{};
    if (8 >= work.hex_ntime.size()) {
        hex2bin((unsigned char*)(&ntime), work.hex_ntime.c_str(), 4);
        ntime = swab32(ntime);
    } else {
        printf("Expected `ntime` with size 8 got size %lu\n", work.hex_ntime.size());
    }

    // Version
    work.native_data[0] = 0x40;
    work.native_data[1] = 0x00;
    work.native_data[2] = 0x00;
    work.native_data[3] = 0x00;

    memcpy(work.native_data + 4, work.prev_block_hash, 32);

    sha256d((unsigned char*)work.merkle_root, coinbase, coinbase_size);
    memcpy(work.native_data + 36, work.merkle_root, 32);

    // reverse merkle root...why?  because bitcoin
    for (int i = 0; i < 16; i++) {
        unsigned char tmp = work.merkle_root[i];
        work.merkle_root[i] = work.merkle_root[31 - i];
        work.merkle_root[31 - i] = tmp;
    }

    memcpy(work.native_data + 68, &ntime, 4);

    unsigned char bits[4];
    hex2bin(bits, nbits.data(), nbits.size());
    memcpy(work.native_data + 72, &bits[3], 1);
    memcpy(work.native_data + 73, &bits[2], 1);
    memcpy(work.native_data + 74, &bits[1], 1);
    memcpy(work.native_data + 75, &bits[0], 1);

    // set work number for reloading
    work.num = ++shared_work.num;
}

int main(int argc, char* argv[]) {
    printf("*******************************************************************\n");
    printf("Dynamo coin reference miner.  This software is supplied by Dynamo\n");
    printf("Coin Foundation with no warranty and solely on an AS-IS basis.\n");
    printf("\n");
    printf("We hope others will use this as a code base to produce production\n");
    printf("quality mining software.\n");
    printf("\n");
    printf("Version %s, July 4, 2021\n", minerVersion);
    printf("*******************************************************************\n");

    dyn_miner miner{};

#ifdef GPU_MINER
    miner.gpu_program.kernel.print();
    printf("\n");
#endif

    if (argc != 8) {
        printf("usage: dyn_miner <RPC host> <RPC port> <RPC username> <RPC password> <CPU|GPU> "
               "<num CPU threads|num GPU compute units> <gpu platform id>\n\n");
        printf("EXAMPLE:\n");
        printf("    dyn_miner testnet1.dynamocoin.org 6433 user password CPU 4 0\n");
        printf("    dyn_miner testnet1.dynamocoin.org 6433 user password GPU 1000 0\n");
        printf("\n");
        printf("In CPU mode the program will create N number of CPU threads.\n");
        printf("In GPU mode, the program will create N number of compute units.\n");
        printf("platform ID (starts at 0) is for multi GPU systems.  Ignored for CPU.\n");
        printf("pool mode enables use with dyn miner pool, solo is for standalone mining.\n");

        return -1;
    }

    rpc_config_t rpc;
    rpc.host = argv[1];
    rpc.port = atoi(argv[2]);
    rpc.user = argv[3];
    rpc.password = argv[4];

    miner.compute_units = atoi(argv[6]);
    miner.gpu_platform_id = atoi(argv[7]);

    if ((toupper(argv[5][0]) != 'C') && (toupper(argv[5][0]) != 'G')) {
        printf("Miner type must be CPU or GPU");
    }
    miner_device device = toupper(argv[5][0]) == 'C' ? miner_device::CPU : miner_device::GPU;

    if (device == miner_device::CPU) {
        for (uint32_t i = 0; i < miner.compute_units; i++) {
            std::thread([i, &miner]() { miner.start_cpu(i); }).detach();
        }
    } else if (device == miner_device::GPU) {
#ifdef GPU_MINER
        for (uint32_t i = 0; i < miner.gpu_program.kernel.numOpenCLDevices; i++) {
            std::thread([i, &miner]() { miner.start_gpu(i); }).detach();
        }
#else
        printf("Not compiled with GPU support.\n");
        return -1;
#endif
    }

    // Start hashrate reporter thread
    std::thread([&miner]() {
        miner.wait_for_work();
        time_t start;
        time(&start);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            time_t now;
            time(&now);
            outputStats(now, start, miner.shares);
        }
    }).detach();

#ifdef __linux__
    signal(SIGPIPE, SIG_IGN);
#endif
    cbuf_t* cbuf = (cbuf_t*)calloc(1, sizeof(cbuf_t));

    while (true) {
        struct hostent* he = gethostbyname(rpc.host);
        if (he == NULL) {
            printf("Cannot resolve host %s\n", rpc.host);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        struct sockaddr_in addr;
        cbuf->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (cbuf->fd < 0) {
            printf("Cannot open socket.\n");
            exit(1);
        }
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(rpc.port);
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        printf("Connecting to %s:%d\n", rpc.host, rpc.port);
        int err = connect(cbuf->fd, (struct sockaddr*)&addr, sizeof(addr));
        if (err != 0) {
            printf("Error connecting to %s:%d\n", rpc.host, rpc.port);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // send authorization message
        char buf[CBSIZE] = {0};
        sprintf(
          buf,
          "{\"params\": [\"%s\", \"%s\"], \"id\": \"auth\", \"method\": \"mining.authorize\"}",
          rpc.user,
          rpc.password);
        DEBUG_LOG("> %s\n", buf);
        size_t w = write(cbuf->fd, buf, strlen(buf));
        if (w < strlen(buf)) {
            printf("Failed to authenticate\n");
            continue;
        }

        // send suggest difficulty
        double suggest_diff = 1;
        sprintf(buf, "{\"params\": [%.8f], \"id\": \"diff\", \"method\": \"mining.suggest_difficulty\"}", suggest_diff);
        write(cbuf->fd, buf, strlen(buf));
        DEBUG_LOG("> %s\n", buf);

        // spawn a thread writing shares
        std::thread([&miner, user = rpc.user, fd = cbuf->fd]() {
            char buf[CBSIZE] = {0};
            uint32_t rpc_id = 0;
            while (true) {
                // wait for new share
                miner.shares.notify.wait(false);
                // clear notification
                miner.shares.notify.clear();

                std::optional<share_t> share_opt = std::nullopt;
                while ((share_opt = miner.shares.pop())) {
                    const share_t share = share_opt.value();
                    if (miner.shared_work != share.job_num) {
                        DEBUG_LOG("Stale share for job %d\n", share.job_num);
                        continue;
                    }
                    const std::string hex_nonce = makeHex((unsigned char*)share.nonce, 4);
                    // write message
                    sprintf(
                      buf,
                      "{\"params\": [\"%s\", \"%s\", \"\", \"%s\", \"%s\"], \"id\": \"%d\", "
                      "\"method\": \"mining.submit\"}",
                      user,
                      share.job_id.c_str(),
                      share.hex_ntime.c_str(),
                      hex_nonce.c_str(),
                      rpc_id);
                    DEBUG_LOG("> %s\n", buf);
                    size_t size = strlen(buf);
                    size_t res = write(fd, buf, size);
                    if (res == -1) {
                        printf("Writing failed, closing loop.\n");
                        return;
                    }
                    rpc_id++;
                }
            }
        }).detach();

        // read messages from socket line by line
        while (read_line(cbuf, buf, sizeof(buf)) > 0) {
            DEBUG_LOG("< %s\n", buf);
            json msg = json::parse(buf);
            const json& id = msg["id"];
            if (id.is_null()) {
                const std::string& method = msg["method"];
                if (method == "mining.notify") {
                    miner.set_job(msg, device);
                } else if (method == "mining.set_difficulty") {
                    const std::vector<double>& params = msg["params"];
                    const double diff = params[0];
                    miner.set_difficulty(diff);
                } else {
                    printf("Unknown stratum method %s\n", method.data());
                }
            } else {
                const std::string& resp = id;
                if (resp == "auth") {
                    const bool result = msg["result"];
                    if (!result) {
                        printf("Failed authentication as %s\n", rpc.user);
                    }
                } else if (resp == "diff") {
                    miner.set_difficulty(suggest_diff);
                } else {
                    const bool result = msg["result"];
                    if (!result) {
                        const std::vector<json>& error = msg["error"];
                        const int code = error[0];
                        const std::string& message = error[1];
                        printf("Error (%s): %s (code: %d)\n", resp.c_str(), message.c_str(), code);
                    } else {
                        miner.shares.accepted_share_count++;
                    }
                }
            }
        }

        // close socket
        shutdown(cbuf->fd, SHUT_RDWR);
        close(cbuf->fd);
    }
}
