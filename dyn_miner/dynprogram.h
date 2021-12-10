#pragma once

#include <string>
#include <vector>
#include <limits>

enum class hashop : uint32_t {
    ADD = 0,
    XOR = 1,
    SHA_SINGLE = 2,
    SHA_LOOP = 3,
    MEMGEN = 4,
    MEMADD = 5,
    MEMXOR = 6,
    MEM_SELECT = 7,
    UNKNOWN = std::numeric_limits<uint32_t>::max(),
};

enum class memregion : uint32_t {
    merkle_root = 0,
    prev_hash = 1,
    unknown = 2,
};

struct program_t {
    std::vector<uint32_t> bytecode{};

    void append_op(hashop);
    void append_hex_hash(const std::string&);

    struct reader_t {
        size_t pos{};
        std::vector<uint32_t> bytecode{};

        inline bool empty() const { return pos == bytecode.size(); }
        inline uint32_t pop() { return bytecode[pos++]; }
        inline uint32_t peek() { return bytecode[pos + 1]; }
        inline uint32_t get(uint32_t index) { return bytecode[pos + index]; }
        inline void adv(uint32_t index) { pos += index; }
        inline hashop read_op() { return static_cast<hashop>(pop()); }
        inline memregion read_memregion() { return static_cast<memregion>(pop()); }
    };

    reader_t reader() const { return {.pos = 0, .bytecode = bytecode}; }
};

program_t program_to_bytecode(const std::vector<std::string>& program);

void execute_program(
  unsigned char* output,
  const unsigned char* blockHeader,
  const program_t& program,
  const char* prev_block_hash,
  const char* merkle_root,
  uint32_t** mempool);
