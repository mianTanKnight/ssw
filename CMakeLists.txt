cmake_minimum_required(VERSION 3.10)
project(ssw C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SSW_CORE_SOURCES "src/*.c")
# 从源文件列表中移除 main.c，因为它属于可执行文件，不属于库
list(REMOVE_ITEM SSW_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c)

add_library(ssw_core  STATIC  ${SSW_CORE_SOURCES})
target_include_directories(ssw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 创建可执行文件 ssw
add_executable(ssw src/main.c)

# 将 ssw 链接到 ssw_core 库
target_link_libraries(ssw PRIVATE ssw_core)

# ======================================================================
# 4. Modular Tests (Configurable)
# ======================================================================
option(BUILD_TESTING "Build the project's tests" ON)

if(BUILD_TESTING)
    enable_testing()

    # --- CMD Module Tests ---
    # Tests for command processing and hash table integration
    file(GLOB CMD_TEST_SOURCES "test/test_cmd_*.c")
    list(REMOVE_ITEM CMD_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/test_cmd_runner.c)

    add_executable(test_cmd test/test_cmd_runner.c ${CMD_TEST_SOURCES})
    target_link_libraries(test_cmd PRIVATE ssw_core)
    target_include_directories(test_cmd PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/test
    )
    add_test(NAME test_cmd COMMAND test_cmd)

    # --- OHashTable Module Tests ---
    # Tests for open-addressed hash table with expiration support
    add_executable(test_ohash_table
        test/test_ohahs_runner.c
        test/test_ohash_table.c
    )
    target_link_libraries(test_ohash_table PRIVATE ssw_core)
    target_include_directories(test_ohash_table PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/test
    )
    add_test(NAME test_ohash_table COMMAND test_ohash_table)

    # --- Protocol Module Tests ---
    # Tests for RESP2 parser with zero-copy and fragmentation handling
    file(GLOB PROTOCOL_TEST_SOURCES "test/test_protocol_*.c")
    list(REMOVE_ITEM PROTOCOL_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/test_protocol_runner.c)

    add_executable(test_protocol
        test/test_protocol_runner.c
        ${PROTOCOL_TEST_SOURCES}
    )
    target_link_libraries(test_protocol PRIVATE ssw_core)
    target_include_directories(test_protocol PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/test
    )
    add_test(NAME test_protocol COMMAND test_protocol)

endif()