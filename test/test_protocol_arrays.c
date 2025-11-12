//
// Created by wenshen on 2025/10/22.
//

//
// Array Protocol Tests
//

#include "test_protocol_framework.h"
#include "../include/resp2parser.h"

void test_array_empty(void) {
    TEST_START("Array: empty *0\\r\\n");

    const char buf[] = "*0\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, ARRAYS, "Type should be ARRAYS");
    ASSERT_EQ(ctx.outframe.array_len, 0, "Array length should be 0");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_single_integer(void) {
    TEST_START("Array: single integer *1\\r\\n:42\\r\\n");

    const char buf[] = "*1\r\n:42\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Parse array header
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Array header should succeed");
    ASSERT_EQ(ctx.outframe.type, ARRAYS, "Type should be ARRAYS");
    ASSERT_EQ(ctx.outframe.array_len, 1, "Array length should be 1");

    // Parse element
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Element should succeed");
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Element type should be NUMERIC");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "42", 2, "Element should be '42'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_multiple_integers(void) {
    TEST_START("Array: multiple integers *3\\r\\n:1\\r\\n:2\\r\\n:3\\r\\n");

    const char buf[] = "*3\r\n:1\r\n:2\r\n:3\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Array header
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 3, "Array length should be 3");

    // Element 1
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "1", 1, "Element 1 should be '1'");

    // Element 2
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "2", 1, "Element 2 should be '2'");

    // Element 3
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "3", 1, "Element 3 should be '3'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_mixed_types(void) {
    TEST_START("Array: mixed types");

    const char buf[] = "*5\r\n+OK\r\n-ERR\r\n:100\r\n$5\r\nhello\r\n*0\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Array header
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 5, "Array should have 5 elements");

    // +OK
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Element 1 should be SIMPLE_STR");

    // -ERR
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, ERRORS, "Element 2 should be ERRORS");

    // :100
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Element 3 should be NUMERIC");

    // $5\r\nhello\r\n
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Element 4 should be BULK_STRINGS");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Content should be 'hello'");

    // *0\r\n
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, ARRAYS, "Element 5 should be ARRAYS");
    ASSERT_EQ(ctx.outframe.array_len, 0, "Nested array should be empty");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_nested_simple(void) {
    TEST_START("Array: nested [[1, 2], [3, 4]]");

    const char buf[] = "*2\r\n*2\r\n:1\r\n:2\r\n*2\r\n:3\r\n:4\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Outer array
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 2, "Outer array should have 2 elements");

    // First nested array
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, ARRAYS, "Element 1 should be array");
    ASSERT_EQ(ctx.outframe.array_len, 2, "Nested array 1 should have 2 elements");

    // Elements of first nested array
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "1", 1, "Should be '1'");

    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "2", 1, "Should be '2'");

    // Second nested array
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, ARRAYS, "Element 2 should be array");
    ASSERT_EQ(ctx.outframe.array_len, 2, "Nested array 2 should have 2 elements");

    // Elements of second nested array
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "3", 1, "Should be '3'");

    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "4", 1, "Should be '4'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_nested_deep(void) {
    TEST_START("Array: deep nesting [[[42]]]");

    const char buf[] = "*1\r\n*1\r\n*1\r\n:42\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Level 1
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 1, "Level 1 array");

    // Level 2
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 1, "Level 2 array");

    // Level 3
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 1, "Level 3 array");

    // Element
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Should be numeric");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "42", 2, "Should be '42'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_redis_command(void) {
    TEST_START("Array: Redis SET command");

    const char buf[] = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Array header
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 3, "Command should have 3 parts");

    // SET
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Command should be bulk string");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "SET", 3, "Command is 'SET'");

    // key
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "key", 3, "Arg1 is 'key'");

    // value
    zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "value", 5, "Arg2 is 'value'");

    cleanup_test_context(&cn);
    TEST_PASS();
}


void run_array_tests(void) {
    TEST_SUITE_START("Array Protocol Tests");

    test_array_empty();
    test_array_single_integer();
    test_array_multiple_integers();
    test_array_mixed_types();
    test_array_nested_simple();
    test_array_nested_deep();
    test_array_redis_command();

    TEST_SUITE_END();
}
