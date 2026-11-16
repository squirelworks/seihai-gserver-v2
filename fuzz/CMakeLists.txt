# Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
#
# Standalone libFuzzer project for #87. Built with Clang only; does not
# pull in the full gs2emu / vcpkg / antlr stack. Configure from this
# directory:
#   CC=clang-18 CXX=clang++-18 cmake -S fuzz -B build-fuzz -G Ninja
#   cmake --build build-fuzz

cmake_minimum_required(VERSION 3.27)
project(seihai-gserver-v2-fuzz CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	message(FATAL_ERROR "fuzz/ requires Clang (libFuzzer).")
endif()

include(FetchContent)
# Same library the server pins; tip after seihai-gs2lib #31 (fuzz + bounds).
FetchContent_Declare(gs2lib
	GIT_REPOSITORY https://github.com/squirelworks/seihai-gs2lib.git
	GIT_TAG        2f4635535a3ad9099fb5c74151435d1e79e652c6
)
set(GS2LIB_TESTS OFF CACHE BOOL "" FORCE)
set(GS2LIB_FUZZ OFF CACHE BOOL "" FORCE)
set(NET ON CACHE BOOL "" FORCE)
set(STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(gs2lib)

set(_sanitize "-fsanitize=fuzzer,address,undefined")

add_executable(fuzz_process_buffer fuzz_process_buffer.cpp)
target_include_directories(fuzz_process_buffer PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../server/include"
	"${gs2lib_SOURCE_DIR}/include"
)
target_link_libraries(fuzz_process_buffer PRIVATE gs2lib)
target_compile_options(fuzz_process_buffer PRIVATE ${_sanitize})
target_link_options(fuzz_process_buffer PRIVATE ${_sanitize})
