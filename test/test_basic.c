//
// Created by wenshen on 2025/10/22.
//
// Basic Protocol Tests
//

#include "test_framework.h"
#include "../protocol/resp2parser.h"

void test_simple_string_ok(void) {
    TEST_START("Simple String: +OK\\r\\n");

    const char buf[] = "+OK\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.state, COMPLETE, "State should be COMPLETE");
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Type should be SIMPLE_STR");
    ASSERT_EQ(ctx.outframe.data_len, 2, "Data length should be 2");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "OK", 2, "Content should be 'OK'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_simple_string_with_spaces(void) {
    TEST_START("Simple String: with spaces");

    const char buf[] = "+Hello World\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Type should be SIMPLE_STR");
    ASSERT_EQ(ctx.outframe.data_len, 11, "Data length should be 11");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "Hello World", 11, "Content should match");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_simple_string_empty(void) {
    TEST_START("Simple String: empty +\\r\\n");

    const char buf[] = "+\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Type should be SIMPLE_STR");
    ASSERT_EQ(ctx.outframe.data_len, 0, "Data length should be 0");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_error_string(void) {
    TEST_START("Error String: -ERR unknown command\\r\\n");

    const char buf[] = "-ERR unknown command\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, ERRORS, "Type should be ERRORS");
    ASSERT_EQ(ctx.outframe.data_len, 19, "Data length should be 19");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "ERR unknown command", 19, "Content should match");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_integer_zero(void) {
    TEST_START("Integer: :0\\r\\n");

    const char buf[] = ":0\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Type should be NUMERIC");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "0", 1, "Content should be '0'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_integer_positive(void) {
    TEST_START("Integer: :42\\r\\n");

    const char buf[] = ":42\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Type should be NUMERIC");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "42", 2, "Content should be '42'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_integer_large(void) {
    TEST_START("Integer: :9223372036854775807\\r\\n (LLONG_MAX)");

    const char buf[] = ":9223372036854775807\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Type should be NUMERIC");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "9223372036854775807", 19, "Content should match");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_simple(void) {
    TEST_START("Bulk String: $5\\r\\nhello\\r\\n");

    const char buf[] = "$5\r\nhello\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.state, COMPLETE, "State should be COMPLETE");
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Type should be BULK_STRINGS");
    ASSERT_EQ(ctx.outframe.data_len, 5, "Data length should be 5");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Content should be 'hello'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_empty(void) {
    TEST_START("Bulk String: $0\\r\\n\\r\\n");

    const char buf[] = "$0\r\n\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Type should be BULK_STRINGS");
    ASSERT_EQ(ctx.outframe.data_len, 0, "Data length should be 0");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_with_crlf(void) {
    TEST_START("Bulk String: with embedded \\r\\n");

    const char buf[] = "$12\r\nhello\r\nworld\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Type should be BULK_STRINGS");
    ASSERT_EQ(ctx.outframe.data_len, 12, "Data length should be 12");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello\r\nworld", 12, "Content should contain \\r\\n");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_binary(void) {
    TEST_START("Bulk String: binary data with nulls");

    char buf[32];
    int pos = 0;
    pos += sprintf(buf + pos, "$10\r\n");
    buf[pos++] = 'a';
    buf[pos++] = '\0'; // null byte
    buf[pos++] = 'b';
    buf[pos++] = '\0';
    buf[pos++] = 'c';
    buf[pos++] = '\r';
    buf[pos++] = '\n';
    buf[pos++] = 'd';
    buf[pos++] = 'e';
    buf[pos++] = 'f';
    buf[pos++] = '\r';
    buf[pos++] = '\n';

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Type should be BULK_STRINGS");
    ASSERT_EQ(ctx.outframe.data_len, 10, "Data length should be 10");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void run_basic_tests(void) {
    TEST_SUITE_START("Basic Protocol Tests");

    test_simple_string_ok();
    test_simple_string_with_spaces();
    test_simple_string_empty();

    test_error_string();

    test_integer_zero();
    test_integer_positive();
    test_integer_large();

    test_bulk_string_simple();
    test_bulk_string_empty();
    test_bulk_string_with_crlf();
    test_bulk_string_binary();

    TEST_SUITE_END();
}

