//
// Performance & Benchmark Tests
//

#include "test_framework.h"
#include "../protocol/resp2parser.h"
#include <time.h>
#include <sys/time.h>

// High-resolution timer

static inline double get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000.0 + ts.tv_nsec;
}

// ============================================================================
// Single Frame Performance
// ============================================================================

void test_perf_simple_string(void) {
    TEST_START("Performance: Simple String (+OK\\r\\n)");

    const char buf[] = "+OK\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 1000000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput = 1000000000.0 / avg_ns;

    printf("\n");
    printf("    Iterations:  %d\n", iterations);
    printf("    Avg Time:    %.2f ns/op\n", avg_ns);
    printf("    Throughput:  %.2f M ops/sec\n", throughput / 1000000.0);

    ASSERT_LT(avg_ns, 100, "Average time should be < 100ns");

    TEST_PASS();
}

void test_perf_integer(void) {
    TEST_START("Performance: Integer (:42\\r\\n)");

    const char buf[] = ":42\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 1000000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput = 1000000000.0 / avg_ns;

    printf("\n");
    printf("    Iterations:  %d\n", iterations);
    printf("    Avg Time:    %.2f ns/op\n", avg_ns);
    printf("    Throughput:  %.2f M ops/sec\n", throughput / 1000000.0);

    ASSERT_LT(avg_ns, 100, "Average time should be < 100ns");

    TEST_PASS();
}

void test_perf_bulk_string_small(void) {
    TEST_START("Performance: Bulk String ($5\\r\\nhello\\r\\n)");

    const char buf[] = "$5\r\nhello\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 1000000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput = 1000000000.0 / avg_ns;

    printf("\n");
    printf("    Iterations:  %d\n", iterations);
    printf("    Avg Time:    %.2f ns/op\n", avg_ns);
    printf("    Throughput:  %.2f M ops/sec\n", throughput / 1000000.0);

    ASSERT_LT(avg_ns, 150, "Average time should be < 150ns");

    TEST_PASS();
}

void test_perf_bulk_string_large(void) {
    TEST_START("Performance: Bulk String (1KB)");

    // Construct: $1024\r\n<1024 bytes>\r\n
    char *buf = malloc(1024 + 64);
    int pos = sprintf(buf, "$1024\r\n");
    memset(buf + pos, 'X', 1024);
    pos += 1024;
    memcpy(buf + pos, "\r\n", 2);
    pos += 2;

    const int iterations = 100000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, buf, pos);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput = 1000000000.0 / avg_ns;

    printf("\n");
    printf("    Iterations:  %d\n", iterations);
    printf("    Avg Time:    %.2f ns/op\n", avg_ns);
    printf("    Throughput:  %.2f M ops/sec\n", throughput / 1000000.0);
    printf("    Data Rate:   %.2f MB/sec\n", 1024.0 * throughput / 1048576.0);

    free(buf);
    TEST_PASS();
}

void test_perf_array_header(void) {
    TEST_START("Performance: Array Header (*3\\r\\n)");

    const char buf[] = "*3\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 1000000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput = 1000000000.0 / avg_ns;

    printf("\n");
    printf("    Iterations:  %d\n", iterations);
    printf("    Avg Time:    %.2f ns/op\n", avg_ns);
    printf("    Throughput:  %.2f M ops/sec\n", throughput / 1000000.0);

    ASSERT_LT(avg_ns, 100, "Average time should be < 100ns");

    TEST_PASS();
}

// ============================================================================
// Complex Protocol Performance
// ============================================================================

void test_perf_redis_set_command(void) {
    TEST_START("Performance: Redis SET command");

    const char buf[] = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 100000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();

        // Parse array header
        zerocopy_proceed(&ctx);

        // Parse 3 elements
        zerocopy_proceed(&ctx);
        zerocopy_proceed(&ctx);
        zerocopy_proceed(&ctx);

        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput = 1000000000.0 / avg_ns;

    printf("\n");
    printf("    Iterations:  %d\n", iterations);
    printf("    Avg Time:    %.2f ns/command\n", avg_ns);
    printf("    Throughput:  %.2f K commands/sec\n", throughput / 1000.0);

    TEST_PASS();
}

void test_perf_nested_array(void) {
    TEST_START("Performance: Nested array [[1,2],[3,4]]");

    const char buf[] = "*2\r\n*2\r\n:1\r\n:2\r\n*2\r\n:3\r\n:4\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 100000;
    double total_time_ns = 0;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();

        // Parse: outer array + 2 inner arrays + 4 elements = 7 frames
        for (int i = 0; i < 7; i++) {
            zerocopy_proceed(&ctx);
        }

        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double avg_per_frame = avg_ns / 7.0;

    printf("\n");
    printf("    Iterations:     %d\n", iterations);
    printf("    Avg Time:       %.2f ns/structure\n", avg_ns);
    printf("    Avg per frame:  %.2f ns/frame\n", avg_per_frame);

    TEST_PASS();
}

// ============================================================================
// Throughput Tests
// ============================================================================

void test_perf_throughput_mixed(void) {
    TEST_START("Performance: Mixed protocol throughput");

    // Multiple frames: +OK\r\n:42\r\n$5\r\nhello\r\n-ERR\r\n
    const char buf[] = "+OK\r\n:42\r\n$5\r\nhello\r\n-ERR\r\n";
    struct connection_t cn;
    struct parser_context ctx;

    const int iterations = 100000;
    double total_time_ns = 0;
    size_t total_bytes = (sizeof(buf) - 1) * iterations;

    for (int iter = 0; iter < iterations; iter++) {
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();

        // Parse 4 frames
        while (zerocopy_proceed(&ctx) == 0 && ctx.state == COMPLETE) {
            if (cn.rb_offset >= cn.rb_size) break;
        }

        double end = get_time_ns();

        total_time_ns += (end - start);
        cleanup_test_context(&cn);
    }

    double avg_ns = total_time_ns / iterations;
    double throughput_mbs = (total_bytes / (total_time_ns / 1000000000.0)) / 1048576.0;

    printf("\n");
    printf("    Iterations:   %d\n", iterations);
    printf("    Total Bytes:  %.2f MB\n", total_bytes / 1048576.0);
    printf("    Avg Time:     %.2f ns/batch\n", avg_ns);
    printf("    Throughput:   %.2f MB/sec\n", throughput_mbs);

    TEST_PASS();
}

// ============================================================================
// Latency Distribution
// ============================================================================

void test_perf_latency_distribution(void) {
    TEST_START("Performance: Latency distribution (p50, p95, p99)");

    const char buf[] = "$10\r\nhelloworld\r\n";
    const int iterations = 100000;

    double *latencies = malloc(sizeof(double) * iterations);
    ASSERT_NOT_NULL(latencies, "malloc should succeed");

    for (int iter = 0; iter < iterations; iter++) {
        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, buf, sizeof(buf) - 1);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        latencies[iter] = end - start;
        cleanup_test_context(&cn);
    }

    // Sort latencies
    for (int i = 0; i < iterations - 1; i++) {
        for (int j = i + 1; j < iterations; j++) {
            if (latencies[j] < latencies[i]) {
                double tmp = latencies[i];
                latencies[i] = latencies[j];
                latencies[j] = tmp;
            }
        }
    }

    double p50 = latencies[iterations * 50 / 100];
    double p95 = latencies[iterations * 95 / 100];
    double p99 = latencies[iterations * 99 / 100];
    double p999 = latencies[iterations * 999 / 1000];
    double max = latencies[iterations - 1];

    printf("\n");
    printf("    Iterations: %d\n", iterations);
    printf("    p50:        %.2f ns\n", p50);
    printf("    p95:        %.2f ns\n", p95);
    printf("    p99:        %.2f ns\n", p99);
    printf("    p99.9:      %.2f ns\n", p999);
    printf("    max:        %.2f ns\n", max);

    free(latencies);
    TEST_PASS();
}

// ============================================================================
// Zero-Copy Verification
// ============================================================================

void test_perf_zero_copy_verification(void) {
    TEST_START("Performance: Zero-copy verification");

    const char buf[] = "$1024\r\n";
    char data[1024];
    memset(data, 'X', 1024);

    char *full_buf = malloc(1024 + 64);
    memcpy(full_buf, buf, strlen(buf));
    memcpy(full_buf + strlen(buf), data, 1024);
    memcpy(full_buf + strlen(buf) + 1024, "\r\n", 2);
    size_t total_len = strlen(buf) + 1024 + 2;

    struct connection_t cn;
    struct parser_context ctx;
    setup_test_context(&cn, &ctx, full_buf, total_len);

    zerocopy_proceed(&ctx);

    // Verify pointer points to original buffer
    ASSERT_TRUE(ctx.outframe.start_rbp >= cn.read_buffer &&
                ctx.outframe.start_rbp < cn.read_buffer + cn.rb_size,
                "start_rbp should point to read_buffer (zero-copy)");

    // Verify no data copy
    ASSERT_TRUE(ctx.outframe.start_rbp == cn.read_buffer + strlen(buf),
                "start_rbp should point directly to data");

    printf("\n");
    printf("    ✓ Pointer points to original buffer\n");
    printf("    ✓ No data copying detected\n");
    printf("    ✓ True zero-copy implementation\n");

    cleanup_test_context(&cn);
    free(full_buf);
    TEST_PASS();
}

// ============================================================================
// Comparison with naive implementation
// ============================================================================

void test_perf_comparison_with_copy(void) {
    TEST_START("Performance: vs. naive copy implementation");

    const char buf[] = "$100\r\n";
    char data[100];
    memset(data, 'X', 100);

    char *full_buf = malloc(256);
    sprintf(full_buf, "$100\r\n");
    memcpy(full_buf + 6, data, 100);
    memcpy(full_buf + 106, "\r\n", 2);

    const int iterations = 100000;

    // Test zero-copy version
    double zero_copy_time = 0;
    for (int i = 0; i < iterations; i++) {
        struct connection_t cn;
        struct parser_context ctx;
        setup_test_context(&cn, &ctx, full_buf, 108);

        double start = get_time_ns();
        zerocopy_proceed(&ctx);
        double end = get_time_ns();

        zero_copy_time += (end - start);
        cleanup_test_context(&cn);
    }

    // Test naive copy version (simulation)
    double copy_time = 0;
    for (int i = 0; i < iterations; i++) {
        char *copy_buf = malloc(100);

        double start = get_time_ns();
        memcpy(copy_buf, data, 100);  // Simulate copying
        double end = get_time_ns();

        copy_time += (end - start);
        free(copy_buf);
    }
    copy_time += zero_copy_time;  // Add parsing time

    double zero_copy_avg = zero_copy_time / iterations;
    double copy_avg = copy_time / iterations;
    double speedup = copy_avg / zero_copy_avg;

    printf("\n");
    printf("    Zero-copy:   %.2f ns/op\n", zero_copy_avg);
    printf("    With copy:   %.2f ns/op\n", copy_avg);
    printf("    Speedup:     %.2fx faster\n", speedup);

    free(full_buf);
    TEST_PASS();
}

// void run_performance_tests(void) {
//     TEST_SUITE_START("Performance & Benchmark Tests");
//
//     printf("\n" COLOR_YELLOW "  Note: Performance tests may take a while...\n" COLOR_RESET);
//
//     // Single frame performance
//     test_perf_simple_string();
//     test_perf_integer();
//     test_perf_bulk_string_small();
//     test_perf_bulk_string_large();
//     test_perf_array_header();
//
//     // Complex protocol
//     test_perf_redis_set_command();
//     test_perf_nested_array();
//
//     // Throughput
//     test_perf_throughput_mixed();
//
//     // Latency distribution
//     test_perf_latency_distribution();
//
//     // Zero-copy verification
//     test_perf_zero_copy_verification();
//     test_perf_comparison_with_copy();
//
//     TEST_SUITE_END();
// }