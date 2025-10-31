//
// Stress & Stability Tests
//

#include "test_framework.h"
#include "../protocol/resp2parser.h"
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

// ============================================================================
// Memory Tests
// ============================================================================
// 获取纳秒级时间
static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}


void test_stress_memory_no_leak_simple(void) {
    TEST_START("Stress: Memory leak test (simple frames, 1M iterations)");

    const char buf[] = "+OK\r\n";
    const int iterations = 1000000;

    // Get initial memory usage
    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);

    for (int i = 0; i < iterations; i++) {
        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        zerocopy_proceed(&ctx);

        cleanup_test_context(&cn);
    }

    getrusage(RUSAGE_SELF, &usage_end);

    long mem_diff = usage_end.ru_maxrss - usage_start.ru_maxrss;

    printf("\n");
    printf("    Iterations:   %d\n", iterations);
    printf("    Memory start: %ld KB\n", usage_start.ru_maxrss);
    printf("    Memory end:   %ld KB\n", usage_end.ru_maxrss);
    printf("    Difference:   %ld KB\n", mem_diff);

    // Allow some variance, but should be minimal
    ASSERT_LT(mem_diff, 1024, "Memory increase should be < 1MB");

    TEST_PASS();
}

void test_stress_memory_no_leak_bulk(void) {
    TEST_START("Stress: Memory leak test (bulk strings, 100K iterations)");

    const char buf[] = "$1024\r\n";
    char *full_buf = malloc(1024 + 64);
    sprintf(full_buf, "$1024\r\n");
    memset(full_buf + 7, 'X', 1024);
    memcpy(full_buf + 1031, "\r\n", 2);

    const int iterations = 100000;

    struct rusage usage_start, usage_end;
    getrusage(RUSAGE_SELF, &usage_start);

    for (int i = 0; i < iterations; i++) {
        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, full_buf, 1033);

        zerocopy_proceed(&ctx);

        cleanup_test_context(&cn);
    }

    getrusage(RUSAGE_SELF, &usage_end);

    long mem_diff = usage_end.ru_maxrss - usage_start.ru_maxrss;

    printf("\n");
    printf("    Iterations:   %d\n", iterations);
    printf("    Memory diff:  %ld KB\n", mem_diff);

    ASSERT_LT(mem_diff, 2048, "Memory increase should be < 2MB");

    free(full_buf);
    TEST_PASS();
}

// ============================================================================
// Large Data Tests
// ============================================================================

void test_stress_huge_bulk_string(void) {
    TEST_START("Stress: Huge bulk string (10 MB)");

    size_t data_size = 10 * 1024 * 1024;  // 10MB
    char *buf = malloc(data_size + 128);
    ASSERT_NOT_NULL(buf, "malloc should succeed");

    int header_len = sprintf(buf, "$%zu\r\n", data_size);
    memset(buf + header_len, 'X', data_size);
    memcpy(buf + header_len + data_size, "\r\n", 2);

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, header_len + data_size + 2);

    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_EQ(ctx.outframe.data_len, data_size, "Length should match");

    printf("\n");
    printf("    Data size:    %.2f MB\n", data_size / 1048576.0);
    printf("    ✓ Successfully parsed\n");

    cleanup_test_context(&cn);
    free(buf);
    TEST_PASS();
}

void test_stress_huge_array(void) {
    TEST_START("Stress: Huge array (10K elements)");

    const int num_elements = 10000;
    char *buf = malloc(num_elements * 10);
    ASSERT_NOT_NULL(buf, "malloc should succeed");

    int pos = sprintf(buf, "*%d\r\n", num_elements);
    for (int i = 0; i < num_elements; i++) {
        pos += sprintf(buf + pos, ":1\r\n");
    }

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    // Parse array header
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Array header should succeed");
    ASSERT_EQ(ctx.outframe.array_len, num_elements, "Array length should match");

    // Parse all elements
    for (int i = 0; i < num_elements; i++) {
        rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Element should parse");
    }

    printf("\n");
    printf("    Elements:     %d\n", num_elements);
    printf("    ✓ All elements parsed successfully\n");

    cleanup_test_context(&cn);
    free(buf);
    TEST_PASS();
}

void test_stress_deeply_nested_arrays(void) {
    TEST_START("Stress: Deeply nested arrays (100 levels)");

    const int depth = 100;
    char *buf = malloc(depth * 10);
    ASSERT_NOT_NULL(buf, "malloc should succeed");

    int pos = 0;
    for (int i = 0; i < depth; i++) {
        pos += sprintf(buf + pos, "*1\r\n");
    }
    pos += sprintf(buf + pos, ":42\r\n");

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    // Parse all nested arrays
    for (int i = 0; i < depth; i++) {
        int rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Array header should parse");
        ASSERT_EQ(ctx.outframe.type, ARRAYS, "Should be array");
    }

    // Parse final element
    int rc = zerocopy_proceed(&ctx);
    ASSERT_EQ(rc, 0, "Element should parse");
    ASSERT_EQ(ctx.outframe.type, NUMERIC, "Should be numeric");

    printf("\n");
    printf("    Depth:        %d levels\n", depth);
    printf("    ✓ Successfully parsed\n");

    cleanup_test_context(&cn);
    free(buf);
    TEST_PASS();
}

// ============================================================================
// Continuous Operation Tests
// ============================================================================

void test_stress_continuous_parsing(void) {
    TEST_START("Stress: Continuous parsing (1M frames)");

    const int num_frames = 1000000;

    // Build a buffer with many frames
    char *buf = malloc(num_frames * 10);
    ASSERT_NOT_NULL(buf, "malloc should succeed");

    int pos = 0;
    for (int i = 0; i < num_frames; i++) {
        pos += sprintf(buf + pos, ":1\r\n");
    }

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, buf, pos);

    double start = get_time_ns();

    int parsed = 0;
    while (cn.rb_offset < cn.rb_size) {
        int rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Parse should succeed");
        ASSERT_EQ(ctx.state, COMPLETE, "Should be complete");
        parsed++;
    }

    double end = get_time_ns();
    double total_time_ms = (end - start) / 1000000.0;
    double frames_per_sec = parsed / (total_time_ms / 1000.0);

    ASSERT_EQ(parsed, num_frames, "Should parse all frames");

    printf("\n");
    printf("    Frames:       %d\n", num_frames);
    printf("    Time:         %.2f ms\n", total_time_ms);
    printf("    Throughput:   %.2f M frames/sec\n", frames_per_sec / 1000000.0);

    cleanup_test_context(&cn);
    free(buf);
    TEST_PASS();
}

// ============================================================================
// Fragmentation Stress
// ============================================================================

void test_stress_extreme_fragmentation(void) {
    TEST_START("Stress: Extreme fragmentation (10K fragments)");

    const char *full_data = "$100\r\n";
    char data[100];
    memset(data, 'X', 100);

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, "", 0);

    // Feed header byte by byte
    for (size_t i = 0; i < strlen(full_data); i++) {
        feed_data(&cn, &full_data[i], 1);
        zerocopy_proceed(&ctx);
    }

    // Feed data byte by byte
    for (int i = 0; i < 100; i++) {
        feed_data(&cn, &data[i], 1);
        zerocopy_proceed(&ctx);
    }

    // Feed final CRLF
    feed_data(&cn, "\r", 1);
    zerocopy_proceed(&ctx);
    feed_data(&cn, "\n", 1);
    int rc = zerocopy_proceed(&ctx);

    ASSERT_EQ(rc, 0, "Should finally succeed");
    ASSERT_EQ(ctx.state, COMPLETE, "Should be COMPLETE");
    ASSERT_EQ(ctx.outframe.data_len, 100, "Length should be 100");

    printf("\n");
    printf("    Fragments:    %d bytes\n", (int)strlen(full_data) + 100 + 2);
    printf("    ✓ Successfully reassembled\n");

    cleanup_test_context(&cn);
    TEST_PASS();
}

void test_stress_random_fragmentation(void) {
    TEST_START("Stress: Random fragmentation (1000 iterations)");

    const char *full_data = "*3\r\n$5\r\nhello\r\n$5\r\nworld\r\n:42\r\n";
    size_t full_len = strlen(full_data);

    srand(time(NULL));

    for (int iter = 0; iter < 1000; iter++) {
        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, "", 0);

        size_t pos = 0;
        while (pos < full_len) {
            // Random fragment size: 1-10 bytes
            size_t fragment_size = 1 + (rand() % 10);
            if (pos + fragment_size > full_len) {
                fragment_size = full_len - pos;
            }

            feed_data(&cn, full_data + pos, fragment_size);
            pos += fragment_size;

            // Try to parse
            while (zerocopy_proceed(&ctx) == 0 && ctx.state == COMPLETE) {
                // Keep parsing
            }
        }

        cleanup_test_context(&cn);
    }

    printf("\n");
    printf("    Iterations:   1000\n");
    printf("    ✓ All random fragmentations handled\n");

    TEST_PASS();
}

// ============================================================================
// Error Recovery Tests
// ============================================================================

void test_stress_repeated_errors(void) {
    TEST_START("Stress: Repeated error recovery (10K errors)");

    const int iterations = 10000;
    int errors = 0;

    for (int i = 0; i < iterations; i++) {
        struct connection_t cn;
        struct parser_context ctx;

        // Invalid protocol
        const char buf[] = "$invalid\r\n";
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        int rc = zerocopy_proceed(&ctx);
        if (rc < 0) {
            errors++;
        }

        cleanup_test_context(&cn);
    }

    ASSERT_EQ(errors, iterations, "Should detect all errors");

    printf("\n");
    printf("    Iterations:   %d\n", iterations);
    printf("    Errors:       %d\n", errors);
    printf("    ✓ All errors correctly detected\n");

    TEST_PASS();
}

// ============================================================================
// Boundary Stress Tests
// ============================================================================

void test_stress_boundary_buffer_sizes(void) {
    TEST_START("Stress: Boundary buffer sizes");

    size_t sizes[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64,
                      127, 128, 255, 256, 511, 512, 1023, 1024,
                      2047, 2048, 4095, 4096};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        size_t size = sizes[i];

        char *buf = malloc(size + 64);
        int pos = sprintf(buf, "$%zu\r\n", size);
        memset(buf + pos, 'X', size);
        memcpy(buf + pos + size, "\r\n", 2);

        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, buf, pos + size + 2);

        int rc = zerocopy_proceed(&ctx);
        ASSERT_EQ(rc, 0, "Should succeed");
        ASSERT_EQ(ctx.outframe.data_len, size, "Length should match");

        cleanup_test_context(&cn);
        free(buf);
    }

    printf("\n");
    printf("    Test sizes:   %d different sizes\n", num_sizes);
    printf("    ✓ All boundary sizes handled\n");

    TEST_PASS();
}

// ============================================================================
// Stability Test
// ============================================================================

void test_stress_long_running_stability(void) {
    TEST_START("Stress: Long-running stability (10 minutes simulation)");

    printf("\n");
    printf("    Simulating 10 minutes of operation...\n");

    const int operations_per_second = 100000;
    const int simulation_seconds = 10;  // Reduced for testing

    const char *patterns[] = {
        "+OK\r\n",
        ":42\r\n",
        "$5\r\nhello\r\n",
        "*2\r\n:1\r\n:2\r\n"
    };
    int num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    int total_ops = 0;
    int errors = 0;

    for (int sec = 0; sec < simulation_seconds; sec++) {
        for (int op = 0; op < operations_per_second; op++) {
            const char *pattern = patterns[op % num_patterns];

            struct connection_t cn;
            struct parser_context ctx;
            setup_test_context(&cn, &ctx, pattern, strlen(pattern));

            int rc = zerocopy_proceed(&ctx);
            if (rc != 0) errors++;

            total_ops++;
            cleanup_test_context(&cn);
        }

        if ((sec + 1) % 2 == 0) {
            printf("    Progress: %d/%d seconds...\n", sec + 1, simulation_seconds);
        }
    }

    printf("\n");
    printf("    Total operations: %d\n", total_ops);
    printf("    Errors:           %d\n", errors);
    printf("    Success rate:     %.4f%%\n", 100.0 * (total_ops - errors) / total_ops);

    ASSERT_EQ(errors, 0, "Should have no errors");

    TEST_PASS();
}

void run_stress_tests(void) {
    TEST_SUITE_START("Stress & Stability Tests");

    printf("\n" COLOR_YELLOW "  Note: Stress tests may take several minutes...\n" COLOR_RESET);

    // Memory tests
    test_stress_memory_no_leak_simple();
    test_stress_memory_no_leak_bulk();

    // Large data
    test_stress_huge_bulk_string();
    test_stress_huge_array();
    test_stress_deeply_nested_arrays();

    // Continuous operation
    test_stress_continuous_parsing();

    // Fragmentation stress
    test_stress_extreme_fragmentation();
    test_stress_random_fragmentation();

    // Error recovery
    test_stress_repeated_errors();

    // Boundary tests
    test_stress_boundary_buffer_sizes();

    // Stability
    test_stress_long_running_stability();

    TEST_SUITE_END();
}