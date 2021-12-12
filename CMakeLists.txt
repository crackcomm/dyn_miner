cmake_minimum_required(VERSION 3.12.0)

project(dyn-miner VERSION 0.0.1 LANGUAGES CXX)

option(USE_ASM "Use ASM for SHA256 hashing" ON)
option(GPU_MINER "Build with GPU miner" OFF)
option(DEBUG_LOGS "Build with debug logs" OFF)
option(NATIVE_BUILD "Native build" OFF)

if(USE_ASM)
  add_compile_definitions(USE_ASM ENABLE_AVX2 ENABLE_SSE41)
endif()

if(GPU_MINER)
  add_compile_definitions(GPU_MINER)
endif()

if(DEBUG_LOGS)
  add_compile_definitions(DEBUG_LOGS)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -DNDEBUG")
if(NATIVE_BUILD)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
endif()

add_subdirectory(dyn_miner)

