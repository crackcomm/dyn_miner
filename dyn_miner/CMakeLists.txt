
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

# On linux it conflicts with already existing cpuid headers
if(WIN32)
  target_include_directories(dyn_miner PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/cpuid)
  target_link_libraries(dyn_miner PUBLIC wsock32 ws2_32)
else()
  target_link_libraries(dyn_miner PUBLIC pthread)
endif()

if(GPU_MINER)
  target_link_libraries(dyn_miner PUBLIC OpenCL)
  add_custom_command(
    TARGET dyn_miner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/dyn_miner.cl
            ${CMAKE_CURRENT_BINARY_DIR}/dyn_miner.cl)
endif()

set_property(TARGET dyn_miner PROPERTY CXX_STANDARD 20)
set_property(TARGET dyn_miner PROPERTY CXX_STANDARD_REQUIRED ON)

