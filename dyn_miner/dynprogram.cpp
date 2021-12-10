#include "dynprogram.h"

#include "sha256.h"
#include "util/hex.h"

#include <array>
#include <cstring>
#include <iterator>
#include <sstream>

memregion parse_memregion(const std::string& name) {
    if (name == "MERKLE") {
        return memregion::merkle_root;
    }
    if (name == "HASHPREV") {
        return memregion::prev_hash;
    }
    return memregion::unknown;
}

hashop parse_hashop(const std::vector<std::string>& tokens) {
    const std::string& op = tokens[0];
    if (op == "ADD") {
        return hashop::ADD;
    }
    if (op == "XOR") {
        return hashop::XOR;
    }
    if (op == "SHA2") {
        if (tokens.size() == 2) {
            return hashop::SHA_LOOP;
        }
        return hashop::SHA_SINGLE;
    }
    if (op == "MEMGEN") {
        return hashop::MEMGEN;
    }
    if (op == "MEMADD") {
        return hashop::MEMADD;
    }
    if (op == "MEMXOR") {
        return hashop::MEMXOR;
    }
    if (op == "READMEM") {
        return hashop::MEM_SELECT;
    }
    return hashop::UNKNOWN;
}

void program_t::append_op(hashop op) { bytecode.push_back(static_cast<uint32_t>(op)); }

void program_t::append_hex_hash(const std::string& hex) {
    uint32_t arg1[8];
    parseHex(hex, (unsigned char*)arg1);
    for (uint32_t i = 0; i < 8; i++) {
        bytecode.push_back(arg1[i]);
    }
}

program_t program_to_bytecode(const std::vector<std::string>& program) {
    program_t builder{};
    for (uint32_t line = 0; line < program.size(); line++) {
        std::istringstream iss(program[line]);
        std::vector<std::string> tokens{
          std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{}}; // split line into tokens

        hashop op = parse_hashop(tokens);
        builder.append_op(op);
        switch (op) {
        case hashop::ADD:
            builder.append_hex_hash(tokens[1]);
            break;
        case hashop::XOR:
            builder.append_hex_hash(tokens[1]);
            break;
        case hashop::SHA_LOOP:
            builder.bytecode.push_back(atoi(tokens[1].c_str()));
            break;
        case hashop::SHA_SINGLE:
            break;
        case hashop::MEMGEN:
            builder.append_op(parse_hashop({tokens[1]}));
            builder.bytecode.push_back(atoi(tokens[2].c_str()));
            break;
        case hashop::MEMADD:
            builder.append_hex_hash(tokens[1]);
            break;
        case hashop::MEMXOR:
            builder.append_hex_hash(tokens[1]);
            break;
        case hashop::MEM_SELECT:
            builder.bytecode.push_back(static_cast<uint32_t>(parse_memregion(tokens[1])));
        case hashop::UNKNOWN:
            break;
        }
    }
    return builder;
}

void execute_program(
  unsigned char* output,
  const unsigned char* blockHeader,
  const program_t& program,
  const char* prev_block_hash,
  const char* merkle_root,
  mempool_t& mempool) {
    // initial input is SHA256 of header data
    CSHA256 ctx;
    ctx.Write(blockHeader, 80);
    uint32_t temp_result[8];
    ctx.Finalize((unsigned char*)temp_result);

    uint32_t mem_size = 0; // size of current memory pool

    auto reader = program.reader();
    while (!reader.empty()) {
        const hashop op = reader.read_op();
        switch (op) {
        case hashop::ADD:
            for (uint32_t i = 0; i < 8; i++)
                temp_result[i] += reader.pop();
            break;
        case hashop::XOR:
            for (uint32_t i = 0; i < 8; i++)
                temp_result[i] ^= reader.pop();
            break;
        case hashop::SHA_SINGLE:
            ctx.Reset();
            ctx.Write((unsigned char*)temp_result, 32);
            ctx.Finalize((unsigned char*)temp_result);
            break;
        case hashop::SHA_LOOP: {
            const uint32_t iters = reader.pop();
            for (uint32_t i = 0; i < iters; i++) {
                ctx.Reset();
                ctx.Write((unsigned char*)temp_result, 32);
                ctx.Finalize((unsigned char*)temp_result);
            }
            break;
        }
        case hashop::MEMGEN: {
            const hashop hash_op = reader.read_op();
            const uint32_t new_mem_size = reader.pop();
            mempool.resize(new_mem_size * 32);
            mem_size = new_mem_size;
            if (hash_op == hashop::SHA_SINGLE) {
                for (uint32_t i = 0; i < mem_size; i++) {
                    ctx.Reset();
                    ctx.Write((unsigned char*)temp_result, 32);
                    ctx.Finalize((unsigned char*)temp_result);
                    memcpy(mempool.get() + i * 8, temp_result, 32);
                }
            }
            break;
        }
        case hashop::MEMADD:
            if (mem_size != 0) {
                for (uint32_t i = 0; i < mem_size; i++) {
                    for (int j = 0; j < 8; j++)
                        mempool[i * 8 + j] += reader.get(j);
                }
            }
            reader.adv(8);
            break;
        case hashop::MEMXOR:
            if (mem_size != 0) {
                for (uint32_t i = 0; i < mem_size; i++) {
                    for (int j = 0; j < 8; j++)
                        mempool[i * 8 + j] ^= reader.get(j);
                }
            }
            reader.adv(8);
            break;
        case hashop::MEM_SELECT: {
            const memregion reg = reader.read_memregion();
            switch (reg) {
            case memregion::merkle_root: {
                uint32_t v0 = *(uint32_t*)merkle_root;
                uint32_t index = v0 % mem_size;
                memcpy(temp_result, mempool.get() + index * 8, 32);
                break;
            }
            case memregion::prev_hash: {
                uint32_t v0 = *(uint32_t*)prev_block_hash;
                uint32_t index = v0 % mem_size;
                memcpy(temp_result, mempool.get() + index * 8, 32);
                break;
            }
            case memregion::unknown:
                break;
            }
            break;
        }
        case hashop::UNKNOWN:
            break;
        }
    }
    memcpy(output, temp_result, 32);
}
