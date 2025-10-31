cmake_minimum_required(VERSION 4.0)
project(ssw C)

set(CMAKE_C_STANDARD 11)

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
        test/test_stress.c
        storage/xxhash.h
        storage/xxhash.c
        storage/ohashtable.c
        storage/ohashtable.h
        test/test_ohashtable.c
        test/test_benchmark4ohashtable.c
)




