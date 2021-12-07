#pragma once

#include "dynprogram.h"
#include "util/difficulty.h"
#include "util/hex.h" // TODO: remove, only for debug

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <string>

struct rpc_config_t {
    char* host;
    int port;
    char* user;
    char* password;
    std::string miner_pay_to_addr;
};

struct stats_t {
    std::atomic<uint32_t> nonce_count{};
    std::atomic<uint32_t> share_count{};
    std::atomic<uint32_t> accepted_share_count{};
    std::atomic<uint32_t> rejected_share_count{};
    std::atomic<uint32_t> latest_diff{};
};

// mining.submit:
// [0]: username
// [1]: job_id
// [2]: extranonce2 0000000000000000
// [3]: ntime
// [4]: nonce
struct share_t {
    uint32_t job_num;
    std::string job_id;
    std::string hex_ntime;
    char nonce[4] = {0};
};

struct shares_t {
    std::queue<share_t> queue;
    std::mutex mutex;
    stats_t stats{};
    std::atomic_flag notify = ATOMIC_FLAG_INIT;

    std::optional<share_t> pop() {
        std::unique_lock<std::mutex> _lock(mutex);
        if (queue.empty()) {
            return std::nullopt;
        }
        share_t share = queue.front();
        queue.pop();
        return share;
    }

    void append(share_t share) {
        std::unique_lock<std::mutex> _lock(mutex);
        queue.push(share);
        stats.share_count++;
        [[maybe_unused]] bool value = notify.test_and_set(std::memory_order_acquire);
        notify.notify_one();
    }
};

inline std::vector<std::string> load_program(std::string strProgram, char delim) {
    std::stringstream stream(strProgram);
    std::string line;
    std::vector<std::string> program{};
    while (std::getline(stream, line, delim)) {
        program.push_back(line);
    }
    return program;
}

constexpr double diff_multiplier = 65536;

struct work_t {
    uint32_t num = 0;
    std::string job_id{};
    std::string hex_ntime{};
    char prev_block_hash[32] = {0};
    char merkle_root[32] = {0};
    uint64_t share_target{};
    unsigned char native_data[80] = {0};
    std::vector<std::string> program{};
    std::string str_program{};
    program_t cpu_program{};

    bool set_program(const std::string& new_program_str) {
        if (new_program_str == str_program) {
            return false;
        }
        program = load_program(new_program_str, '$');
        str_program = new_program_str;
        cpu_program = program_to_bytecode(program);
        return true;
    }

    share_t share(char* nonce) const {
        share_t share{.job_num = num, .job_id = job_id, .hex_ntime = hex_ntime};
        memcpy(share.nonce, nonce, 4);
        return share;
    }

    share_t share(uint32_t nonce) const {
        share_t share{.job_num = num, .job_id = job_id, .hex_ntime = hex_ntime};
        memcpy(share.nonce, &nonce, 4);
        return share;
    }

    void set_difficulty(double diff) {
        diff = std::max(diff, 1.0);
        share_target = static_cast<uint64_t>(share_to_target(diff) * diff_multiplier);
    }
};

struct shared_work_t {
    work_t work{};
    std::shared_mutex mutex{};
    std::atomic<std::uint32_t> num{};

    work_t clone() {
        std::shared_lock<std::shared_mutex> _lock(mutex);
        return work;
    }

    void set_difficulty(double diff) {
        std::unique_lock<std::shared_mutex> _lock(mutex);
        work.set_difficulty(diff);
        if (work.num != 0) {
            work.num = ++num;
        }
    }

    bool operator==(const uint32_t& n) const { return num.load(std::memory_order_relaxed) == n; }
    bool operator!=(const uint32_t& n) const { return num.load(std::memory_order_relaxed) != n; }
    bool operator==(const work_t& work) const { return num.load(std::memory_order_relaxed) == work.num; }
    bool operator!=(const work_t& work) const { return num.load(std::memory_order_relaxed) != work.num; }
};
