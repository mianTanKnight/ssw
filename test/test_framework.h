//
// Created by wenshen on 2025/10/22.
//

//
// Professional Test Framework for RESP2 Parser
//
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include "../protocol/resp2parser.h"

// ANSI Color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_MAGENTA "\033[0;35m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_BOLD    "\033[1m"

// Test statistics
typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
    double total_time_ms;
} test_stats_t;

static test_stats_t g_stats = {0};
static struct timeval g_test_start;
static const char *g_current_test = NULL;
static const char *g_current_suite = NULL;

// Macros
#define TEST_SUITE_START(name) \
    do { \
        g_current_suite = name; \
        printf("\n" COLOR_BOLD COLOR_CYAN "╔════════════════════════════════════════════════════════════╗\n"); \
        printf("║  Test Suite: %-45s ║\n", name); \
        printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n"); \
    } while(0)

#define TEST_SUITE_END() \
    do { \
        printf("\n" COLOR_CYAN "  Suite '%s' completed\n" COLOR_RESET, g_current_suite); \
        g_current_suite = NULL; \
    } while(0)

#define TEST_START(name) \
    do { \
        g_current_test = name; \
        g_stats.total++; \
        gettimeofday(&g_test_start, NULL); \
        printf("  [TEST] %s ... ", name); \
        fflush(stdout); \
    } while(0)

#define TEST_PASS() \
    do { \
        struct timeval end; \
        gettimeofday(&end, NULL); \
        double elapsed = (end.tv_sec - g_test_start.tv_sec) * 1000.0 + \
                        (end.tv_usec - g_test_start.tv_usec) / 1000.0; \
        g_stats.passed++; \
        g_stats.total_time_ms += elapsed; \
        printf(COLOR_GREEN "✓ PASS" COLOR_RESET " (%.2f ms)\n", elapsed); \
        g_current_test = NULL; \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        g_stats.failed++; \
        printf(COLOR_RED "✗ FAIL" COLOR_RESET "\n"); \
        printf("    " COLOR_RED "Error: %s" COLOR_RESET "\n", msg); \
        g_current_test = NULL; \
        return; \
    } while(0)

#define TEST_SKIP(reason) \
    do { \
        g_stats.skipped++; \
        printf(COLOR_YELLOW "⊘ SKIP" COLOR_RESET " (%s)\n", reason); \
        g_current_test = NULL; \
        return; \
    } while(0)

// Assertions
#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            char buf[512]; \
            snprintf(buf, sizeof(buf), "%s (condition: %s)", msg, #cond); \
            TEST_FAIL(buf); \
        } \
    } while(0)

#define ASSERT_FALSE(cond, msg) \
    ASSERT_TRUE(!(cond), msg)

#define ASSERT_EQ(actual, expected, msg) \
    do { \
        long long _a = (long long)(actual); \
        long long _e = (long long)(expected); \
        if (_a != _e) { \
            char buf[512]; \
            snprintf(buf, sizeof(buf), "%s\n    Expected: %lld\n    Got: %lld", \
                    msg, _e, _a); \
            TEST_FAIL(buf); \
        } \
    } while(0)

#define ASSERT_NE(actual, unexpected, msg) \
    do { \
        long long _a = (long long)(actual); \
        long long _u = (long long)(unexpected); \
        if (_a == _u) { \
            char buf[512]; \
            snprintf(buf, sizeof(buf), "%s\n    Should not be: %lld", msg, _u); \
            TEST_FAIL(buf); \
        } \
    } while(0)

#define ASSERT_LT(actual, expected, msg) \
    do { \
        long long _a = (long long)(actual); \
        long long _e = (long long)(expected); \
        if (_a >= _e) { \
            char buf[512]; \
            snprintf(buf, sizeof(buf), "%s\n    Expected: < %lld\n    Got: %lld", \
                    msg, _e, _a); \
            TEST_FAIL(buf); \
        } \
    } while(0)

#define ASSERT_GT(actual, expected, msg) \
    do { \
        long long _a = (long long)(actual); \
        long long _e = (long long)(expected); \
        if (_a <= _e) { \
            char buf[512]; \
            snprintf(buf, sizeof(buf), "%s\n    Expected: > %lld\n    Got: %lld", \
                    msg, _e, _a); \
            TEST_FAIL(buf); \
        } \
    } while(0)

#define ASSERT_STR_EQ(actual, expected, len, msg) \
    do { \
        if (memcmp(actual, expected, len) != 0) { \
            char buf[512]; \
            snprintf(buf, sizeof(buf), "%s\n    Expected: '%.*s'\n    Got: '%.*s'", \
                    msg, (int)len, expected, (int)len, actual); \
            TEST_FAIL(buf); \
        } \
    } while(0)

#define ASSERT_NULL(ptr, msg) \
    ASSERT_TRUE((ptr) == NULL, msg)

#define ASSERT_NOT_NULL(ptr, msg) \
    ASSERT_TRUE((ptr) != NULL, msg)

// Test report
static inline void print_test_report(void) {
    printf("\n");
    printf(COLOR_BOLD "╔════════════════════════════════════════════════════════════╗\n");
    printf("║                     TEST SUMMARY                           ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Total Tests:    %-42d ║\n", g_stats.total);

    if (g_stats.passed > 0) {
        printf("║  " COLOR_GREEN "✓ Passed:       %-42d" COLOR_RESET " ║\n", g_stats.passed);
    }
    if (g_stats.failed > 0) {
        printf("║  " COLOR_RED "✗ Failed:       %-42d" COLOR_RESET " ║\n", g_stats.failed);
    }
    if (g_stats.skipped > 0) {
        printf("║  " COLOR_YELLOW "⊘ Skipped:      %-42d" COLOR_RESET " ║\n", g_stats.skipped);
    }

    printf("║  Total Time:     %-38.2f ms ║\n", g_stats.total_time_ms);

    if (g_stats.total > 0) {
        double pass_rate = (g_stats.passed * 100.0) / g_stats.total;
        printf("║  Success Rate:   %38.2f%% ║\n", pass_rate);
    }

    printf("╚════════════════════════════════════════════════════════════╝" COLOR_RESET "\n");

    if (g_stats.failed == 0) {
        printf("\n" COLOR_GREEN COLOR_BOLD "  🎉 ALL TESTS PASSED! 🎉\n" COLOR_RESET "\n");
    } else {
        printf("\n" COLOR_RED COLOR_BOLD "  ❌ SOME TESTS FAILED\n" COLOR_RESET "\n");
    }
}

// Helper function: setup test context
static inline void setup_test_context(struct connection_t *cn,
                                      struct parser_context *ctx,
                                      const char *data,
                                      size_t len) {
    memset(cn, 0, sizeof(*cn));
    cn->read_buffer = (char *) malloc(len + 1);
    assert(cn->read_buffer);
    memcpy(cn->read_buffer, data, len);
    cn->read_buffer[len] = '\0';
    cn->rb_size = len;
    cn->rb_offset = 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->connection = cn;
    ctx->state = COMPLETE;
}

// Helper function: cleanup test context
static inline void cleanup_test_context(struct connection_t *cn) {
    if (cn->read_buffer) {
        free(cn->read_buffer);
        cn->read_buffer = NULL;
    }
}

// Helper function: feed data incrementally (for fragmentation tests)
static inline void feed_data(struct connection_t *cn, const char *data, size_t len) {
    size_t new_size = cn->rb_size + len;
    char *new_buf = realloc(cn->read_buffer, new_size + 1);
    assert(new_buf);

    memcpy(new_buf + cn->rb_size, data, len);
    new_buf[new_size] = '\0';

    cn->read_buffer = new_buf;
    cn->rb_size = new_size;
}

void run_basic_tests(void);

void run_array_tests(void);

void run_edge_case_tests(void);

void run_fragmentation_tests(void);

void run_performance_tests(void);

void run_stress_tests(void);

#endif // TEST_FRAMEWORK_H
