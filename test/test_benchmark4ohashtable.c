//
// Created by wenshen on 2025/10/31.
////
// Performance Benchmark for ohashtable
// Compare with Redis and other implementations
//

#include "../storage/ohashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#define COLOR_CYAN    "\x1b[36m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_RESET   "\x1b[0m"

// ============================================================================
// Timing and Stats
// ============================================================================

typedef struct {
    double elapsed_ms;
    long ops_count;
    double ops_per_sec;
    double avg_latency_us;
    double p50_latency_us;
    double p99_latency_us;
    size_t memory_kb;
} benchmark_result_t;

static inline double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

static size_t get_memory_usage_kb(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss; // KB on Linux
}

static void print_result(const char *name, benchmark_result_t *result) {
    printf(COLOR_CYAN "%-30s" COLOR_RESET " │ ", name);
    printf("%7.2f ms │ ", result->elapsed_ms);
    printf("%10.0f ops/s │ ", result->ops_per_sec);
    printf("%6.2f μs │ ", result->avg_latency_us);
    printf("%7zu KB\n", result->memory_kb);
}

// ============================================================================
// Latency Tracking
// ============================================================================

typedef struct {
    double *samples;
    size_t count;
    size_t capacity;
} latency_tracker_t;

static latency_tracker_t *tracker_create(size_t capacity) {
    latency_tracker_t *t = malloc(sizeof(latency_tracker_t));
    t->samples = malloc(capacity * sizeof(double));
    t->count = 0;
    t->capacity = capacity;
    return t;
}

static void tracker_add(latency_tracker_t *t, double latency_us) {
    if (t->count < t->capacity) {
        t->samples[t->count++] = latency_us;
    }
}

static int compare_double(const void *a, const void *b) {
    double da = *(const double *) a;
    double db = *(const double *) b;
    return (da > db) - (da < db);
}

static double tracker_percentile(latency_tracker_t *t, double p) {
    if (t->count == 0) return 0;
    qsort(t->samples, t->count, sizeof(double), compare_double);
    size_t idx = (size_t) (t->count * p / 100.0);
    if (idx >= t->count) idx = t->count - 1;
    return t->samples[idx];
}

static void tracker_destroy(latency_tracker_t *t) {
    free(t->samples);
    free(t);
}

// ============================================================================
// Helper Functions
// ============================================================================

static char *gen_key(int i) {
    char buf[64];
    snprintf(buf, sizeof(buf), "benchmark_key_%d", i);
    return strdup(buf);
}

static int *gen_value(int i) {
    int *v = malloc(sizeof(int));
    *v = i;
    return v;
}

static void cleanup_all(void) {
    if (ohashtabl) {
        for (uint64_t i = 0; i < cap; i++) {
            if (!ohashtabl[i].rm) {
                free(ohashtabl[i].key);
                free(ohashtabl[i].v);
            }
        }
        free(ohashtabl);
        ohashtabl = NULL;
        cap = 0;
        size = 0;
    }
}

// ============================================================================
// Benchmarks
// ============================================================================

benchmark_result_t benchmark_insert_sequential(int n) {
    benchmark_result_t result = {0};
    initohash(n / 2); // Start smaller to test expansion

    size_t mem_before = get_memory_usage_kb();
    double start = get_time_us();

    for (int i = 0; i < n; i++) {
        char *key = gen_key(i);
        int *value = gen_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    double end = get_time_us();
    size_t mem_after = get_memory_usage_kb();

    result.elapsed_ms = (end - start) / 1000.0;
    result.ops_count = n;
    result.ops_per_sec = n / ((end - start) / 1000000.0);
    result.avg_latency_us = (end - start) / n;
    result.memory_kb = mem_after - mem_before;

    return result;
}

benchmark_result_t benchmark_get_sequential(int n) {
    benchmark_result_t result = {0};

    // Setup: insert keys
    initohash(n);
    for (int i = 0; i < n; i++) {
        char *key = gen_key(i);
        int *value = gen_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    latency_tracker_t *tracker = tracker_create(n);
    double start = get_time_us();

    for (int i = 0; i < n; i++) {
        char key_buf[64];
        snprintf(key_buf, sizeof(key_buf), "benchmark_key_%d", i);

        double op_start = get_time_us();
        oget(key_buf, strlen(key_buf));
        double op_end = get_time_us();

        tracker_add(tracker, op_end - op_start);
    }

    double end = get_time_us();

    result.elapsed_ms = (end - start) / 1000.0;
    result.ops_count = n;
    result.ops_per_sec = n / ((end - start) / 1000000.0);
    result.avg_latency_us = (end - start) / n;
    result.p50_latency_us = tracker_percentile(tracker, 50);
    result.p99_latency_us = tracker_percentile(tracker, 99);

    tracker_destroy(tracker);
    cleanup_all();

    return result;
}

benchmark_result_t benchmark_get_random(int n) {
    benchmark_result_t result = {0};

    // Setup
    initohash(n);
    for (int i = 0; i < n; i++) {
        char *key = gen_key(i);
        int *value = gen_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    // Generate random indices
    int *indices = malloc(n * sizeof(int));
    srand(42); // Fixed seed for reproducibility
    for (int i = 0; i < n; i++) {
        indices[i] = rand() % n;
    }

    latency_tracker_t *tracker = tracker_create(n);
    double start = get_time_us();

    for (int i = 0; i < n; i++) {
        char key_buf[64];
        snprintf(key_buf, sizeof(key_buf), "benchmark_key_%d", indices[i]);

        double op_start = get_time_us();
        oget(key_buf, strlen(key_buf));
        double op_end = get_time_us();

        tracker_add(tracker, op_end - op_start);
    }

    double end = get_time_us();

    result.elapsed_ms = (end - start) / 1000.0;
    result.ops_count = n;
    result.ops_per_sec = n / ((end - start) / 1000000.0);
    result.avg_latency_us = (end - start) / n;
    result.p50_latency_us = tracker_percentile(tracker, 50);
    result.p99_latency_us = tracker_percentile(tracker, 99);

    free(indices);
    tracker_destroy(tracker);
    cleanup_all();

    return result;
}

benchmark_result_t benchmark_delete_sequential(int n) {
    benchmark_result_t result = {0};

    // Setup
    initohash(n);
    for (int i = 0; i < n; i++) {
        char *key = gen_key(i);
        int *value = gen_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    double start = get_time_us();

    for (int i = 0; i < n; i++) {
        char key_buf[64];
        snprintf(key_buf, sizeof(key_buf), "benchmark_key_%d", i);
        oret_t ret;
        otake(key_buf, strlen(key_buf), &ret);
        if (ret.key) {
            free(ret.key);
            free(ret.value);
        }
    }

    double end = get_time_us();

    result.elapsed_ms = (end - start) / 1000.0;
    result.ops_count = n;
    result.ops_per_sec = n / ((end - start) / 1000000.0);
    result.avg_latency_us = (end - start) / n;

    cleanup_all();

    return result;
}

benchmark_result_t benchmark_mixed_workload(int n) {
    benchmark_result_t result = {0};
    initohash(n / 2);

    double start = get_time_us();

    // 60% insert, 30% get, 10% delete
    for (int i = 0; i < n; i++) {
        int op = i % 10;

        if (op < 6) {
            // Insert
            char *key = gen_key(i);
            int *value = gen_value(i);
            oret_t ret = {0};
            int s = oinsert(key, strlen(key), value, 0, &ret);
            if (s == REPLACED) {
                free(ret.value);
                free(key);
            }
        } else if (op < 9) {
            // Get
            char key_buf[64];
            snprintf(key_buf, sizeof(key_buf), "benchmark_key_%d", i / 2);
            oget(key_buf, strlen(key_buf));
        } else {
            // Delete
            char key_buf[64];
            snprintf(key_buf, sizeof(key_buf), "benchmark_key_%d", i / 3);
            oret_t ret = {0};
            otake(key_buf, strlen(key_buf), &ret);
            if (ret.key) {
                free(ret.key);
                free(ret.value);
            }
        }
    }

    double end = get_time_us();

    result.elapsed_ms = (end - start) / 1000.0;
    result.ops_count = n;
    result.ops_per_sec = n / ((end - start) / 1000000.0);
    result.avg_latency_us = (end - start) / n;

    cleanup_all();

    return result;
}

benchmark_result_t benchmark_high_collision(int n) {
    benchmark_result_t result = {0};
    initohash(64); // Small table to force collisions

    double start = get_time_us();

    for (int i = 0; i < n; i++) {
        char *key = gen_key(i);
        int *value = gen_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    double end = get_time_us();

    result.elapsed_ms = (end - start) / 1000.0;
    result.ops_count = n;
    result.ops_per_sec = n / ((end - start) / 1000000.0);
    result.avg_latency_us = (end - start) / n;

    cleanup_all();

    return result;
}

// ============================================================================
// Scalability Test
// ============================================================================

void benchmark_scalability(void) {
    printf("\n" COLOR_YELLOW "=== Scalability Analysis ===" COLOR_RESET "\n");
    printf("Testing with increasing data sizes...\n\n");

    int sizes[] = {1000, 10000, 100000, 1000000};
    int n_sizes = sizeof(sizes) / sizeof(sizes[0]);

    printf("┌──────────┬────────────┬─────────────┬──────────────┐\n");
    printf("│   Size   │   Time     │ Throughput  │    Latency   │\n");
    printf("├──────────┼────────────┼─────────────┼──────────────┤\n");

    for (int i = 0; i < n_sizes; i++) {
        benchmark_result_t r = benchmark_insert_sequential(sizes[i]);
        printf("│ %7d  │ %7.2f ms │ %8.0f/s │ %9.3f μs │\n",
               sizes[i], r.elapsed_ms, r.ops_per_sec, r.avg_latency_us);
    }

    printf("└──────────┴────────────┴─────────────┴──────────────┘\n");
}

// ============================================================================
// Main
// ============================================================================

// int main(int argc, char **argv) {
//     int n = 100000;
//
//     if (argc > 1) {
//         n = atoi(argv[1]);
//         if (n <= 0) n = 100000;
//     }
//
//     printf("\n");
//     printf("╔═══════════════════════════════════════════════════════════════╗\n");
//     printf("║          ohashtable Performance Benchmark                     ║\n");
//     printf("╠═══════════════════════════════════════════════════════════════╣\n");
//     printf("║  Operations: %-10d                                       ║\n", n);
//     printf("║  Struct Size: 32 bytes (cache-optimized)                     ║\n");
//     printf("║  Load Factor: 0.7 (70%%)                                      ║\n");
//     printf("║  Hash: xxHash64                                              ║\n");
//     printf("╚═══════════════════════════════════════════════════════════════╝\n");
//     printf("\n");
//
//     printf("┌──────────────────────────────┬───────────┬────────────────┬──────────┬─────────────┐\n");
//     printf("│ Benchmark                    │   Time    │   Throughput   │ Latency  │   Memory    │\n");
//     printf("├──────────────────────────────┼───────────┼────────────────┼──────────┼─────────────┤\n");
//
//     benchmark_result_t r;
//
//     r = benchmark_insert_sequential(n);
//     print_result("Insert (Sequential)", &r);
//
//     r = benchmark_get_sequential(n);
//     print_result("Get (Sequential)", &r);
//     printf("│                              │           │                │  P50: %-6.2f μs       │\n", r.p50_latency_us);
//     printf("│                              │           │                │  P99: %-6.2f μs       │\n", r.p99_latency_us);
//     printf("├──────────────────────────────┼───────────┼────────────────┼──────────┼─────────────┤\n");
//
//     r = benchmark_get_random(n);
//     print_result("Get (Random)", &r);
//     printf("│                              │           │                │  P50: %-6.2f μs       │\n", r.p50_latency_us);
//     printf("│                              │           │                │  P99: %-6.2f μs       │\n", r.p99_latency_us);
//     printf("├──────────────────────────────┼───────────┼────────────────┼──────────┼─────────────┤\n");
//
//     r = benchmark_delete_sequential(n);
//     print_result("Delete (Sequential)", &r);
//
//     r = benchmark_mixed_workload(n);
//     print_result("Mixed (60/30/10 I/G/D)", &r);
//
//     r = benchmark_high_collision(n);
//     print_result("High Collision", &r);
//
//     printf("└──────────────────────────────┴───────────┴────────────────┴──────────┴─────────────┘\n");
//
//     benchmark_scalability();
//
//     printf("\n" COLOR_YELLOW "Comparison Reference (Typical Redis Performance):" COLOR_RESET "\n");
//     printf("  Redis GET: ~100,000-150,000 ops/sec (single-threaded)\n");
//     printf("  Redis SET: ~80,000-120,000 ops/sec\n");
//     printf("  Note: Redis includes networking, serialization, and command parsing\n");
//     printf("  ohashtable is pure in-memory operations (no network overhead)\n");
//     printf("\n");
//
//     return 0;
// }
