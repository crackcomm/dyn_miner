#include "dynprogram.h"

#include "sha256.h"
#include "util/hex.h"

#include <cstring>
#include <iterator>
#include <sstream>

void execute_program(
  unsigned char* output,
  const unsigned char* blockHeader,
  const std::vector<std::string>& program,
  const char* prev_block_hash,
  const char* merkle_root) {
    // initial input is SHA256 of header data
    CSHA256 ctx;
    ctx.Write(blockHeader, 80);
    uint32_t temp_result[8];
    ctx.Finalize((unsigned char*)temp_result);

    int line_ptr = 0;         // program execution line pointer
    int loop_counter = 0;     // counter for loop execution
    uint32_t memory_size = 0; // size of current memory pool
    uint32_t* memPool = NULL; // memory pool

    while (line_ptr < program.size()) {
        std::istringstream iss(program[line_ptr]);
        std::vector<std::string> tokens{
          std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{}}; // split line into tokens

        // simple ADD and XOR functions with one constant argument
        if (tokens[0] == "ADD") {
            uint32_t arg1[8];
            parseHex(tokens[1], (unsigned char*)arg1);
            for (uint32_t i = 0; i < 8; i++)
                temp_result[i] += arg1[i];
        }

        else if (tokens[0] == "XOR") {
            uint32_t arg1[8];
            parseHex(tokens[1], (unsigned char*)arg1);
            for (uint32_t i = 0; i < 8; i++)
                temp_result[i] ^= arg1[i];
        }

        // hash algo which can be optionally repeated several times
        else if (tokens[0] == "SHA2") {
            if (tokens.size() == 2) { // includes a loop count
                loop_counter = atoi(tokens[1].c_str());
                for (int i = 0; i < loop_counter; i++) {
                    if (tokens[0] == "SHA2") {
                        unsigned char temp[32];
                        ctx.Reset();
                        ctx.Write((unsigned char*)temp_result, 32);
                        ctx.Finalize(temp);
                        memcpy(temp_result, temp, 32);
                    }
                }
            } else { // just a single run
                unsigned char temp[32];
                ctx.Reset();
                ctx.Write((unsigned char*)temp_result, 32);
                ctx.Finalize(temp);
                memcpy(temp_result, temp, 32);
            }
        }

        // generate a block of memory based on a hashing algo
        else if (tokens[0] == "MEMGEN") {
            if (memPool != NULL) free(memPool);
            memory_size = atoi(tokens[2].c_str());
            memPool = (uint32_t*)malloc(memory_size * 32);
            for (uint32_t i = 0; i < memory_size; i++) {
                if (tokens[1] == "SHA2") {
                    unsigned char temp[32];
                    ctx.Reset();
                    ctx.Write((unsigned char*)temp_result, 32);
                    ctx.Finalize(temp);
                    memcpy(temp_result, temp, 32);
                    memcpy(memPool + i * 8, temp_result, 32);
                }
            }
        }

        // add a constant to every value in the memory block
        else if (tokens[0] == "MEMADD") {
            if (memPool != NULL) {
                uint32_t arg1[8];
                parseHex(tokens[1], (unsigned char*)arg1);

                for (uint32_t i = 0; i < memory_size; i++) {
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

                for (uint32_t i = 0; i < memory_size; i++) {
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
                    uint32_t v0 = *(uint32_t*)merkle_root;
                    index = v0 % memory_size;
                    memcpy(temp_result, memPool + index * 8, 32);
                }

                else if (tokens[1] == "HASHPREV") {
                    uint32_t v0 = *(uint32_t*)prev_block_hash;
                    index = v0 % memory_size;
                    memcpy(temp_result, memPool + index * 8, 32);
                }
            }
        }

        line_ptr++;
    }
    if (memPool != NULL) free(memPool);
    memcpy(output, temp_result, 32);
}
