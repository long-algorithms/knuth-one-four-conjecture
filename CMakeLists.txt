cmake_minimum_required(VERSION 3.18)
project(knuth_one_four_conjecture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # GNU __float128 with libquadmath

include(FetchContent)
# Moodycamel concurrentqueue v1.0.5, immutable upstream commit.
FetchContent_Declare(concurrentqueue
  GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
  GIT_TAG 9afb99746f0f5fc94ac8aef737053ae0481ba8d1
)
FetchContent_MakeAvailable(concurrentqueue)
find_package(Threads REQUIRED)

add_executable(knuth_search src/knuth_search.cpp)
target_link_libraries(knuth_search PRIVATE concurrentqueue Threads::Threads quadmath)
target_compile_options(knuth_search PRIVATE -Wall -Wextra)

option(KNUTH_BUILD_HISTORICAL "Build the original, non-thread-safe historical program" OFF)
if(KNUTH_BUILD_HISTORICAL)
  add_executable(knuth_historical historical/concurrent-quad/quad.cpp)
  target_link_libraries(knuth_historical PRIVATE concurrentqueue Threads::Threads quadmath)
endif()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME exact_representations
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/exact_representations.py)
  add_test(NAME thread_consistency
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/thread_consistency.py $<TARGET_FILE:knuth_search>)
endif()
