#pragma once

#include <string>
#include <vector>

void execute_program(
  unsigned char* output,
  const unsigned char* blockHeader,
  const std::vector<std::string>& program,
  const char* prev_block_hash,
  const char* merkle_root);
