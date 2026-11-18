cmake_minimum_required(VERSION 3.16)
project(kvlite VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KVLITE_BUILD_TESTS "Build the GoogleTest suite" ON)
option(KVLITE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(KVLITE_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(kvlite_core
  src/aof.cpp
  src/commands.cpp
  src/store.cpp
)
target_include_directories(kvlite_core PUBLIC include)
if(MSVC)
  target_compile_options(kvlite_core PRIVATE /W4)
else()
  target_compile_options(kvlite_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
endif()

add_executable(kvlite src/main.cpp)
target_link_libraries(kvlite PRIVATE kvlite_core)

if(KVLITE_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
      GIT_SHALLOW TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # MSVC: match the runtime library
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(kvlite_tests
    tests/lru_cache_test.cpp
    tests/store_test.cpp
    tests/aof_test.cpp
    tests/commands_test.cpp
  )
  target_link_libraries(kvlite_tests PRIVATE kvlite_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(kvlite_tests)
endif()
