cmake_minimum_required(VERSION 3.20)
project(minislurm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Warnings are free code review. Keep them on and keep the build clean.
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

option(MSL_SANITIZE "Build with AddressSanitizer + UBSan" ON)
if(MSL_SANITIZE AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- libraries ---------------------------------------------------------------
# M1: the scheduler's math. Pure, no syscalls, fully unit-testable.
add_library(msl_sched
  src/sched/billing.cpp
  src/sched/decay.cpp
  src/sched/fairtree.cpp
  src/sched/priority.cpp
)
target_include_directories(msl_sched PUBLIC src)

# ---- tools -------------------------------------------------------------------
add_executable(msprio-demo tools/msprio_demo.cpp)
target_link_libraries(msprio-demo PRIVATE msl_sched)

# M2: the job runner (slurmstepd-lite). Skeleton with TODOs, you write it.
add_executable(msrun src/exec/msrun.cpp)

# `cmake --build build --target m2-test` runs the shell tests against msrun.
# Not part of ctest yet: they fail until the TODOs are done.
add_custom_target(m2-test
  COMMAND ${CMAKE_COMMAND} -E env MSRUN=$<TARGET_FILE:msrun>
          bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_msrun.sh
  DEPENDS msrun
  USES_TERMINAL)

# ---- tests -------------------------------------------------------------------
enable_testing()
add_executable(msl_tests
  tests/test_main.cpp
  tests/test_billing.cpp
  tests/test_decay.cpp
  tests/test_fairtree.cpp
  tests/test_priority.cpp
)
target_link_libraries(msl_tests PRIVATE msl_sched)
add_test(NAME msl_tests COMMAND msl_tests)
