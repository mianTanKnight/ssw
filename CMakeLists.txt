cmake_minimum_required(VERSION 3.10)
project(ssw C)

set(CMAKE_C_STANDARD 11)

# Enable debug symbols for debugging in CLion
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Debug build: -g (debug symbols) -O0 (no optimization)
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
# Release build: -O3 (optimization) -DNDEBUG
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Main executable
add_executable(ssw main.c
        server_/noblock_sserver.c
        server_/noblock_sserver.h
        errno.h
        protocol/resp2parser.c
        protocol/resp2parser.h
        test/test_framework.h
        test/test_basic.c
        test/test_arrays.c
        test/test_edge_cases.c
        test/test_fragmentation.c
        test/test_performance.c
        test/test_ohashtable.c
        storage/xxhash.h
        storage/xxhash.c
        storage/ohashtable.c
        storage/ohashtable.h
        test/test_benchmark4ohashtable.c
        command/cmd_.c
        command/cmd_.h
        test/test_cmd_runner.c
        test/test_cmd_functional.c
        test/test_cmd_memory.c
        test/test_cmd_performance.c
        test/test_cmd_stress.c
        test/test_framework.h
)

# CMD + OHASH Test Suite
add_executable(test_cmd
        test/test_cmd_runner.c
        test/test_cmd_functional.c
        test/test_cmd_memory.c
        test/test_cmd_performance.c
        test/test_cmd_stress.c
        test/test_framework.h
        storage/xxhash.c
        storage/xxhash.h
        storage/ohashtable.c
        storage/ohashtable.h
        command/cmd_.c
        command/cmd_.h
)

# Enable testing
enable_testing()
add_test(NAME cmd_tests COMMAND test_cmd)




