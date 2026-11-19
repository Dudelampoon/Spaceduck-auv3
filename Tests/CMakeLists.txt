# Tests/CMakeLists.txt — standalone build for the DSP core tests (no JUCE).
# Run from the repo root:
#   cmake -S Tests -B build-tests -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure

cmake_minimum_required(VERSION 3.22)
project(SpaceDuckDSPTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_ducker
    test_ducker.cpp
    ../Source/DSP/SpectralDucker.cpp
)
target_include_directories(test_ducker PRIVATE ../Source/DSP)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_ducker PRIVATE -Wall -Wextra -O2)
endif()

enable_testing()
add_test(NAME ducker COMMAND test_ducker)
