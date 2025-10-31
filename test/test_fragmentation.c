//
// Fragmentation & Packet Splitting Tests (拆包/粘包测试)
//

#include "test_framework.h"
#include "../protocol/resp2parser.h"
#include "stdio.h"

// ============================================================================
// Simple String Fragmentation
// ============================================================================

void test_simple_string_split_at_prefix(void) {
    TEST_START("Fragmentation: split at prefix +|OK\\r\\n");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "+"
    feed_data(&cn, "+", 1);
    int rc = zerocopy_proceed(&ctx);
    printf("zerocopy_proceed rc = %d\n", rc);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING after '+'");

    // Feed: "OK\r\n"
    feed_data(&cn, "OK\r\n", 4);
    int f = zerocopy_proceed(&ctx);
    ASSERT_EQ(f, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "OK", 2, "Content should be 'OK'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_simple_string_split_in_content(void) {
    TEST_START("Fragmentation: split in content +He|llo\\r\\n");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "+He"
    feed_data(&cn, "+He", 3);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING");

    // Feed: "llo\r\n"
    feed_data(&cn, "llo\r\n", 5);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "Hello", 5, "Content should be 'Hello'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_simple_string_split_at_cr(void) {
    TEST_START("Fragmentation: split at \\r (+OK\\r|\\n)");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "+OK\r"
    feed_data(&cn, "+OK\r", 4);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for \\n");

    // Feed: "\n"
    feed_data(&cn, "\n", 1);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "OK", 2, "Content should be 'OK'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_simple_string_one_byte_at_a_time(void) {
    TEST_START("Fragmentation: one byte at a time");

    const char *data = "+HELLO\r\n";
    size_t len = strlen(data);

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed one byte at a time
    for (size_t i = 0; i < len - 1; i++) {
        feed_data(&cn, &data[i], 1);
        int rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(ctx.state, WAITING, "Should be WAITING");
    }

    // Feed last byte
    feed_data(&cn, &data[len - 1], 1);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "HELLO", 5, "Content should be 'HELLO'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Bulk String Fragmentation
// ============================================================================

void test_bulk_string_split_at_header(void) {
    TEST_START("Fragmentation: bulk split at header ($5|\\r\\nhello\\r\\n)");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "$5"
    feed_data(&cn, "$5", 2);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for header CRLF");

    // Feed: "\r\nhello\r\n"
    feed_data(&cn, "\r\nhello\r\n", 9);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Content should be 'hello'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_split_in_data(void) {
    TEST_START("Fragmentation: bulk split in data ($5\\r\\nhel|lo\\r\\n)");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "$5\r\nhel"
    feed_data(&cn, "$5\r\nhel", 7);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for more data");

    // Feed: "lo\r\n"
    feed_data(&cn, "lo\r\n", 4);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Content should be 'hello'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_split_at_final_crlf(void) {
    TEST_START("Fragmentation: bulk split at final CRLF ($5\\r\\nhello\\r|\\n)");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "$5\r\nhello\r"
    feed_data(&cn, "$5\r\nhello\r", 10);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for final \\n");

    // Feed: "\n"
    feed_data(&cn, "\n", 1);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Content should be 'hello'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_three_phase_split(void) {
    TEST_START("Fragmentation: bulk 3-phase split");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Phase 1: "$5\r\n"
    feed_data(&cn, "$5\r\n", 4);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Phase 1: WAITING");

    // Phase 2: "hel"
    feed_data(&cn, "hel", 3);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Phase 2: WAITING");

    // Phase 3: "lo\r\n"
    feed_data(&cn, "lo\r\n", 4);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Phase 3: should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Content should be 'hello'");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Array Fragmentation
// ============================================================================

void test_array_split_at_header(void) {
    TEST_START("Fragmentation: array split at header (*3|\\r\\n:1\\r\\n:2\\r\\n:3\\r\\n)");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "*3"
    feed_data(&cn, "*3", 2);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING");

    // Feed: "\r\n:1\r\n:2\r\n:3\r\n"
    feed_data(&cn, "\r\n:1\r\n:2\r\n:3\r\n", 15);

    // Parse array header
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Array header should succeed");
    ASSERT_EQ(ctx.outframe.array_len, 3, "Array length should be 3");

    // Parse 3 elements
    for (int i = 1; i <= 3; i++) {
        rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Element should succeed");
        char expected[8];
        sprintf(expected, "%d", i);
        ASSERT_STR_EQ(ctx.outframe.start_rbp, expected, 1, "Element value");
    }

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_array_split_between_elements(void) {
    TEST_START("Fragmentation: array split between elements (*2\\r\\n:1\\r\\n|:2\\r\\n)");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "*2\r\n:1\r\n"
    feed_data(&cn, "*2\r\n:1\r\n", 9);

    // Parse array header
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 2, "Array length should be 2");

    // Parse first element
    rc = zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "1", 1, "First element");

    // Try to parse second element (not enough data)
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING for second element");

    // Feed: ":2\r\n"
    feed_data(&cn, ":2\r\n", 4);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "2", 1, "Second element");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_nested_array_fragmentation(void) {
    TEST_START("Fragmentation: nested array [[1, 2]]");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed part 1: "*1\r\n*2\r\n"
    feed_data(&cn, "*1\r\n*2\r\n", 8);

    // Parse outer array
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 1, "Outer array length");

    // Parse inner array
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.outframe.array_len, 2, "Inner array length");

    // Try to parse first element (not enough data)
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Should be WAITING");

    // Feed part 2: ":1\r\n:2\r\n"
    feed_data(&cn, ":1\r\n:2\r\n", 8);

    // Parse first element
    rc = zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "1", 1, "First element");

    // Parse second element
    rc = zerocopy_proceed(&ctx);
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "2", 1, "Second element");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Complex Fragmentation Scenarios
// ============================================================================

void test_multiple_frames_gradual_feed(void) {
    TEST_START("Fragmentation: multiple frames fed gradually");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed: "+OK\r"
    feed_data(&cn, "+OK\r", 4);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Frame 1: WAITING");

    // Feed: "\n:4"
    feed_data(&cn, "\n:4", 3);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Frame 1: should complete");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "OK", 2, "Frame 1 content");

    // Try frame 2
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Frame 2: WAITING");

    // Feed: "2\r\n$5\r\nhello\r\n"
    feed_data(&cn, "2\r\n$5\r\nhello\r\n", 15);

    // Complete frame 2
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Frame 2: should complete");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "42", 2, "Frame 2 content");

    // Parse frame 3
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Frame 3: should complete");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello", 5, "Frame 3 content");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_bulk_string_with_embedded_crlf_fragmented(void) {
    TEST_START("Fragmentation: bulk with embedded \\r\\n");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Data: $12\r\nhello\r\nworld\r\n
    // Split into: "$12\r\nhe" + "llo\r\nwor" + "ld\r\n"

    feed_data(&cn, "$12\r\nhe", 7);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Phase 1: WAITING");

    feed_data(&cn, "llo\r\nwor", 8);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(ctx.state, WAITING, "Phase 2: WAITING");

    feed_data(&cn, "ld\r\n", 4);
    rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should complete");
    ASSERT_EQ(ctx.outframe.data_len, 12, "Length should be 12");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "hello\r\nworld", 12, "Content with \\r\\n");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_extreme_fragmentation(void) {
    TEST_START("Fragmentation: extreme - every byte separate");

    const char *full_data = "$10\r\nhelloworld\r\n";
    size_t len = strlen(full_data);

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed one byte at a time
    for (size_t i = 0; i < len - 1; i++) {
        feed_data(&cn, &full_data[i], 1);
        int rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(ctx.state, WAITING, "Should keep WAITING");
    }

    // Feed last byte
    feed_data(&cn, &full_data[len - 1], 1);
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Should finally complete");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_STR_EQ(ctx.outframe.start_rbp, "helloworld", 10, "Content should match");

    cleanup_test_context(&cn);
    TEST_PASS();
}

// ============================================================================
// Random Split Points
// ============================================================================

void test_random_split_bulk_string(void) {
    TEST_START("Fragmentation: bulk string at random split points");

    const char *full_data = "$20\r\nABCDEFGHIJKLMNOPQRST\r\n";
    size_t len = strlen(full_data);

    // Test with different split points
    int split_points[] = {1, 3, 5, 7, 10, 15, 20, 24};
    int num_splits = sizeof(split_points) / sizeof(split_points[0]);

    for (int s = 0; s < num_splits; s++) {
        int split = split_points[s];

        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, "", 0);

        // Feed first part
        feed_data(&cn, full_data, split);
        int rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(ctx.state, WAITING, "Should be WAITING at split point");

        // Feed remaining part
        feed_data(&cn, full_data + split, len - split);
        rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Should complete");
        ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
        ASSERT_STR_EQ(ctx.outframe.start_rbp, "ABCDEFGHIJKLMNOPQRST", 20, "Content should match");

        cleanup_test_context(&cn);
    }

    TEST_PASS();
}

void run_fragmentation_tests(void) {
    TEST_SUITE_START("Fragmentation & Packet Splitting Tests");

    // Simple string fragmentation
    test_simple_string_split_at_prefix();
    test_simple_string_split_in_content();
    test_simple_string_split_at_cr();
    test_simple_string_one_byte_at_a_time();
    //
    // // Bulk string fragmentation
    test_bulk_string_split_at_header();
    test_bulk_string_split_in_data();
    test_bulk_string_split_at_final_crlf();
    test_bulk_string_three_phase_split();
    //
    // // Array fragmentation
    test_array_split_at_header();
    test_array_split_between_elements();
    test_nested_array_fragmentation();

    // Complex scenarios
    test_multiple_frames_gradual_feed();
    test_bulk_string_with_embedded_crlf_fragmented();
    test_extreme_fragmentation();
    test_random_split_bulk_string();

    TEST_SUITE_END();
}

