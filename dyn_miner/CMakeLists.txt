
if(GPU_MINER)
  set(GPU_MINER_SOURCES dyn_miner_gpu.cpp)
endif()

add_executable(dyn_miner
  core/arith_uint256.cpp
  core/sha256.cpp
  core/uint256.cpp
  dyn_miner.cpp
  dynprogram.cpp
  ${GPU_MINER_SOURCES}
)

target_include_directories(dyn_miner PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dyn_miner PUBLIC pthread)

# On linux it conflicts with already existing cpuid headers
if(WIN32)
  target_include_directories(dyn_miner PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cpuid)
endif()

if(GPU_MINER)
  target_link_libraries(dyn_miner PUBLIC OpenCL)
endif()

