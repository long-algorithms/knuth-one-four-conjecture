cmake_minimum_required(VERSION 3.18)

# Select the compiler before project(): CMake caches it on first configure.
set(KNUTH_ARCH "" CACHE STRING "Optional -march target, e.g. native, znver3, or knl")
set(_knuth_native_xeon_phi FALSE)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_TOOLCHAIN_FILE AND EXISTS "/proc/cpuinfo")
  file(STRINGS "/proc/cpuinfo" _knuth_cpu_model REGEX "^model name[ \\t]*:" LIMIT_COUNT 1)
  if(_knuth_cpu_model MATCHES "Xeon.*Phi")
    set(_knuth_native_xeon_phi TRUE)
  endif()
endif()

# GCC 15 removed -march=knl and -march=knm. Prefer GCC 14 for native Xeon Phi
# and explicitly requested KNL/KNM targets, without overriding CXX, toolchains,
# or a compiler selected in an existing CMake cache.
if((_knuth_native_xeon_phi OR KNUTH_ARCH STREQUAL "knl" OR KNUTH_ARCH STREQUAL "knm")
   AND NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX} AND NOT CMAKE_TOOLCHAIN_FILE)
  find_program(_knuth_gxx14 NAMES g++-14)
  if(NOT _knuth_gxx14)
    message(FATAL_ERROR "Xeon Phi builds default to g++-14, which is not installed. Install g++-14, or explicitly select a compatible compiler using CXX or -DCMAKE_CXX_COMPILER.")
  endif()
  set(CMAKE_CXX_COMPILER "${_knuth_gxx14}" CACHE FILEPATH "C++ compiler for Xeon Phi")
  message(STATUS "Selecting GCC 14 for Xeon Phi: ${_knuth_gxx14}")
endif()

project(knuth_one_four_conjecture LANGUAGES CXX)

# Native KNL builds are tuned by default when a supporting compiler is used.
# An explicit KNUTH_ARCH choice always takes precedence.
if(_knuth_native_xeon_phi AND NOT KNUTH_ARCH
   AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
  set(KNUTH_ARCH "knl" CACHE STRING "Optional -march target, e.g. native, znver3, or knl" FORCE)
  message(STATUS "Detected Xeon Phi: enabling -march=knl")
endif()
if((KNUTH_ARCH STREQUAL "knl" OR KNUTH_ARCH STREQUAL "knm")
   AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
  message(FATAL_ERROR "GCC 15+ removed -march=knl and -march=knm. Use g++-14 and a fresh build directory.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # GNU __float128 / __uint128_t
option(KNUTH_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(KNUTH_FORCE_QUADMATH "Test the mutex-protected lgammaq fallback" OFF)

include(FetchContent)
# Moodycamel concurrentqueue v1.0.5, immutable upstream commit.
FetchContent_Declare(concurrentqueue
  GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
  GIT_TAG 9afb99746f0f5fc94ac8aef737053ae0481ba8d1
)
FetchContent_MakeAvailable(concurrentqueue)
find_package(Threads REQUIRED)
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES quadmath)
check_cxx_source_compiles("#include <quadmath.h>\nint main() { volatile __float128 x=4.0Q; return logq(x)<0; }" KNUTH_HAVE_QUADMATH)
if(NOT KNUTH_HAVE_QUADMATH)
  message(FATAL_ERROR "A compiler supporting __float128 and libquadmath is required (GCC on GNU/Linux).")
endif()
check_cxx_source_compiles("#include <cmath>\n#include <quadmath.h>\nint main() { int sign; volatile __float128 x=4.0Q; return lgammaf128_r(x,&sign)<0; }" KNUTH_HAVE_LGAMMAF128_R)
unset(CMAKE_REQUIRED_LIBRARIES)

add_library(knuth_options INTERFACE)
target_include_directories(knuth_options INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(knuth_options INTERFACE Threads::Threads quadmath)
target_compile_options(knuth_options INTERFACE -Wall -Wextra)
if(KNUTH_HAVE_LGAMMAF128_R AND NOT KNUTH_FORCE_QUADMATH)
  target_compile_definitions(knuth_options INTERFACE KNUTH_HAVE_LGAMMAF128_R=1)
else()
  message(STATUS "Using serialized lgammaq fallback; parallel numerical scaling will be limited")
endif()
if(KNUTH_ARCH)
  target_compile_options(knuth_options INTERFACE -march=${KNUTH_ARCH})
endif()
if(KNUTH_SANITIZERS)
  target_compile_options(knuth_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(knuth_options INTERFACE -fsanitize=address,undefined)
endif()
# Never enable -ffast-math: integer floors depend on numerical boundary behavior.
add_executable(knuth_search src/knuth_search.cpp)
target_link_libraries(knuth_search PRIVATE knuth_options concurrentqueue)

option(KNUTH_BUILD_HISTORICAL "Build the original, non-thread-safe historical program" OFF)
if(KNUTH_BUILD_HISTORICAL)
  add_executable(knuth_historical historical/concurrent-quad/quad.cpp)
  target_link_libraries(knuth_historical PRIVATE concurrentqueue Threads::Threads quadmath)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_executable(knuth_core_tests tests/core.cpp)
  target_link_libraries(knuth_core_tests PRIVATE knuth_options)
  add_test(NAME core COMMAND knuth_core_tests)
  add_test(NAME exact_representations COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/exact_representations.py)
  add_test(NAME search_regression COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/search_regression.py $<TARGET_FILE:knuth_search>)
  set_tests_properties(core search_regression PROPERTIES TIMEOUT 120)
endif()
