//
// Created by wenshen on 2025/10/22.
//

//
// Edge Cases & Error Handling Tests
//

#include "test_framework.h"
#include "../protocol/resp2parser.h"
#include  "string.h"

// ============================================================================
// Invalid Protocol Tests
// ============================================================================

void test_invalid_prefix(void) {
    TEST_START("Invalid: unknown prefix 'x'");

    const char buf[] = "xInvalid\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    // 应该等待有效前缀
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for valid prefix");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_incomplete_crlf_r_only(void) {
    TEST_START("Incomplete: only \\r without \\n");

    const char buf[] = "+OK\r";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for \\n");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_incomplete_no_crlf(void) {
    TEST_START("Incomplete: no CRLF at all");

    const char buf[] = "+OK";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for CRLF");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_empty_buffer(void) {
    TEST_START("Edge: empty buffer");

    const char buf[] = "";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, 0);

    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should return 0");
    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_only_prefix(void) {
    TEST_START("Incomplete: only prefix '+'");

    const char buf[] = "+";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Bulk String Edge Cases
// ============================================================================

void test_bulk_invalid_length_negative(void) {
    TEST_START("Bulk String: negative length $-5\\r\\n");

    const char buf[] = "$-5\r\nhello\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_LT(rc, 0, "Should return error code");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_invalid_length_letters(void) {
    TEST_START("Bulk String: invalid length $abc\\r\\n");

    const char buf[] = "$abc\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_LT(rc, 0, "Should return error code");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_too_large(void) {
    TEST_START("Bulk String: length exceeds BUFFER_SIZE_MAX");

    const char buf[] = "$99999999999\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_LT(rc, 0, "Should return error code");
    ASSERT_EQ(rc, -EMSGSIZE, "Should return -EMSGSIZE");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_incomplete_data(void) {
    TEST_START("Bulk String: incomplete data $10\\r\\nhello");

    const char buf[] = "$10\r\nhello";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should return 0");
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for more data");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_missing_final_crlf(void) {
    TEST_START("Bulk String: missing final \\r\\n");

    const char buf[] = "$5\r\nhelloXX";  // 应该是 \r\n 但是是 XX
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_LT(rc, 0, "Should return error code");

    cleanup_test_context(&cn);
    TEST_PASS();
}


// ============================================================================
// Array Edge Cases
// ============================================================================

void test_array_invalid_length_negative(void) {
    TEST_START("Array: negative length *-3\\r\\n");

    const char buf[] = "*-3\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_LT(rc, 0, "Should return error code");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_too_large(void) {
    TEST_START("Array: length exceeds ARRAY_SIZE_MAX");

    const char buf[] = "*99999999999\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_LT(rc, 0, "Should return error code");
    ASSERT_EQ(rc, -EMSGSIZE, "Should return -EMSGSIZE");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Boundary Value Tests
// ============================================================================

void test_integer_max_long_long(void) {
    TEST_START("Integer: LLONG_MAX (9223372036854775807)");

    const char buf[] = ":9223372036854775807\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Type should be NUMERIC");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "9223372036854775807", 19, "Content should match");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_max_valid(void) {
    TEST_START("Bulk String: large but valid (1MB)");

    size_t data_size = 1024 * 1024;  // 1MB
    char *buf = malloc(data_size + 64);
    ASSERT_NOT_NULL(buf, "malloc should succeed");

    int header_len = sprintf(buf, "$%zu\r\n", data_size);
    memset(buf + header_len, 'X', data_size);
    memcpy(buf + header_len + data_size, "\r\n", 2);

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, header_len + data_size + 2);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Type should be BULK_STRINGS");
    ASSERT_EQ(ctx.outframe.data_len, data_size, "Length should match");

    cleanup_test_context(&cn);
    free(buf);
    TEST_PASS();
}

void test_array_one_thousand_elements(void) {
    TEST_START("Array: 1000 elements");

    char *buf = malloc(32 * 1024);  // 32KB buffer
    ASSERT_NOT_NULL(buf, "malloc should succeed");

    int pos = sprintf(buf, "*1000\r\n");
    for (int i = 0; i < 1000; i++) {
        pos += sprintf(buf + pos, ":1\r\n");
    }

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    // Array header
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Array header should succeed");
    ASSERT_EQ(ctx.outframe.array_len, 1000, "Array should have 1000 elements");

    // Parse all 1000 elements
    for (int i = 0; i < 1000; i++) {
        rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Element should parse successfully");
        ASSERT_EQ(ctx.outframe.type, NUMERIC, "Element should be numeric");
    }

    cleanup_test_context(&cn);
    free(buf);
    TEST_PASS();
}

// ============================================================================
// Special Characters Tests
// ============================================================================

void test_simple_string_with_special_chars(void) {
    TEST_START("Simple String: with special characters");

    const char buf[] = "+Hello\t\nWorld!@#$%\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Type should be SIMPLE_STR");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "Hello\t\nWorld!@#$%", 17, "Should match");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_all_zeros(void) {
    TEST_START("Bulk String: all zero bytes");

    char buf[32];
    int pos = sprintf(buf, "$10\r\n");
    memset(buf + pos, 0, 10);
    pos += 10;
    memcpy(buf + pos, "\r\n", 2);
    pos += 2;

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Type should be BULK_STRINGS");
    ASSERT_EQ(ctx.outframe.data_len, 10, "Length should be 10");

    // Verify all zeros
    for (size_t i = 0; i < 10; i++) {
        ASSERT_EQ(ctx.outframe.start_rbp[i], 0, "Byte should be 0");
    }

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_all_bytes(void) {
    TEST_START("Bulk String: all possible byte values (0-255)");

    char buf[512];
    int pos = sprintf(buf, "$256\r\n");
    for (int i = 0; i < 256; i++) {
        buf[pos++] = (char)i;
    }
    memcpy(buf + pos, "\r\n", 2);
    pos += 2;

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.outframe.data_len, 256, "Length should be 256");

    // Verify all byte values
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ((unsigned char)ctx.outframe.start_rbp[i], i, "Byte value should match");
    }

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Multiple Frames in Buffer (粘包)
// ============================================================================

void test_multiple_simple_strings(void) {
    TEST_START("Multiple: 3 simple strings in one buffer");

    const char buf[] = "+OK\r\n+PONG\r\n+Hello\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // Frame 1
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Frame 1 should succeed");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "OK", 2, "Frame 1 content");

    // Frame 2
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Frame 2 should succeed");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "PONG", 4, "Frame 2 content");

    // Frame 3
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Frame 3 should succeed");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "Hello", 5, "Frame 3 content");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_multiple_mixed_frames(void) {
    TEST_START("Multiple: mixed frame types");

    const char buf[] = "+OK\r\n:42\r\n$5\r\nhello\r\n-ERR\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    // +OK
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Frame 1 type");

    // :42
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Frame 2 type");

    // $5\r\nhello
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, BULK_STRINGS, "Frame 3 type");

    // -ERR
    zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.type, ERRORS, "Frame 4 type");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Whitespace Tests
// ============================================================================

void test_leading_whitespace(void) {
    TEST_START("Edge: leading whitespace before prefix");

    const char buf[] = "   \t\n  +OK\r\n";
    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

    int rc = zerocopy_proceed(&ctx);

    // 解析器会跳过非前缀字符，直到找到有效前缀
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.outframe.type, SIMPLE_STR, "Should find the +OK");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void run_edge_case_tests(void) {
    TEST_SUITE_START("Edge Cases & Error Handling");

    // Invalid protocol
    test_invalid_prefix();
    test_incomplete_crlf_r_only();
    test_incomplete_no_crlf();
    test_empty_buffer();
    test_only_prefix();

    // Bulk string edge cases
    test_bulk_invalid_length_negative();
    test_bulk_invalid_length_letters();
    test_bulk_too_large();
    test_bulk_incomplete_data();
    test_bulk_missing_final_crlf();

    // Array edge cases
    test_array_invalid_length_negative();
    test_array_too_large();

    // Boundary values
    test_integer_max_long_long();
    test_bulk_string_max_valid();
    test_array_one_thousand_elements();

    // Special characters
    test_simple_string_with_special_chars();
    test_bulk_string_all_zeros();
    test_bulk_string_all_bytes();

    // Multiple frames (粘包)
    test_multiple_simple_strings();
    test_multiple_mixed_frames();

    // Whitespace
    test_leading_whitespace();

    TEST_SUITE_END();
}
