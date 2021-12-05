cmake_minimum_required(VERSION 3.7.0)

project(dyn-miner VERSION 0.0.1 LANGUAGES CXX)

option(USE_ASM "Use ASM for SHA256 hashing" ON)
option(GPU_MINER "Build with GPU miner" OFF)
option(DEBUG_LOGS "Build with debug logs" OFF)

if(USE_ASM)
  add_definitions(-DUSE_ASM -DENABLE_AVX2 -DENABLE_SSE41)
endif()

if(GPU_MINER)
  add_definitions(-DGPU_MINER)
endif()

if(DEBUG_LOGS)
  add_definitions(-DDEBUG_LOGS)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

add_subdirectory(dyn_miner)

