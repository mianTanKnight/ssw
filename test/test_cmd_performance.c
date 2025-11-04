//
// Performance Benchmark Tests for CMD + OHASH
// Tests: Throughput, latency, cache efficiency, scaling behavior
//

#include "test_framework.h"
#include "../command/cmd_.h"
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>

// Performance measurement utilities
static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1e6 + tv.tv_usec;
}

static double get_time_ms(void) {
    return get_time_us() / 1000.0;
}

// Helper to generate deterministic keys
static void generate_key(char *buf, size_t bufsize, int index) {
    snprintf(buf, bufsize, "benchmark_key_%08d", index);
}

static void generate_value(char *buf, size_t bufsize, int index) {
    snprintf(buf, bufsize, "benchmark_value_%08d_data", index);
}

// Test 1: Sequential SET throughput
static void test_sequential_set_throughput(void) {
    TEST_START("Sequential SET throughput");

    const int num_ops = 100000;
    char key[64], value[128];

    double start = get_time_us();

    for (int i = 0; i < num_ops; i++) {
        generate_key(key, sizeof(key), i);
        generate_value(value, sizeof(value), i);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        if (ret == FULL) {
            // Trigger expansion and retry
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        }
        ASSERT_TRUE(ret >= 0, "SET should succeed");
    }

    double end = get_time_us();
    double elapsed_ms = (end - start) / 1000.0;
    double ops_per_sec = (num_ops / elapsed_ms) * 1000.0;

    printf("\n      Operations: %d\n", num_ops);
    printf("      Time: %.2f ms\n", elapsed_ms);
    printf("      Throughput: %.2f ops/sec\n", ops_per_sec);
    printf("      Latency: %.3f μs/op\n", (elapsed_ms * 1000.0) / num_ops);

    ASSERT_GT(ops_per_sec, 10000, "Throughput should be > 10K ops/sec");

    TEST_PASS();
}

// Test 2: Sequential GET throughput
static void test_sequential_get_throughput(void) {
    TEST_START("Sequential GET throughput");

    const int num_ops = 100000;
    char key[64], value[128];

    // Pre-populate
    for (int i = 0; i < num_ops; i++) {
        generate_key(key, sizeof(key), i);
        generate_value(value, sizeof(value), i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        }
    }

    // Benchmark GET
    double start = get_time_us();

    for (int i = 0; i < num_ops; i++) {
        generate_key(key, sizeof(key), i);
        osv *result = GET(key, strlen(key));
        ASSERT_NOT_NULL(result, "GET should find key");
    }

    double end = get_time_us();
    double elapsed_ms = (end - start) / 1000.0;
    double ops_per_sec = (num_ops / elapsed_ms) * 1000.0;

    printf("\n      Operations: %d\n", num_ops);
    printf("      Time: %.2f ms\n", elapsed_ms);
    printf("      Throughput: %.2f ops/sec\n", ops_per_sec);
    printf("      Latency: %.3f μs/op\n", (elapsed_ms * 1000.0) / num_ops);

    ASSERT_GT(ops_per_sec, 50000, "GET throughput should be > 50K ops/sec");

    TEST_PASS();
}

// Test 3: Mixed workload (70% GET, 20% SET, 10% DEL)
static void test_mixed_workload(void) {
    TEST_START("Mixed workload (70% GET, 20% SET, 10% DEL)");

    const int num_ops = 50000;
    char key[64], value[128];

    // Pre-populate
    for (int i = 0; i < num_ops / 2; i++) {
        generate_key(key, sizeof(key), i);
        generate_value(value, sizeof(value), i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        }
    }

    int gets = 0, sets = 0, dels = 0;
    double start = get_time_us();

    for (int i = 0; i < num_ops; i++) {
        int op = i % 10;
        int key_idx = i % (num_ops / 2);
        generate_key(key, sizeof(key), key_idx);

        if (op < 7) {
            // GET
            osv *result = GET(key, strlen(key));
            (void) result; // May or may not exist
            gets++;
        } else if (op < 9) {
            // SET
            generate_value(value, sizeof(value), key_idx);
            int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
            if (ret == FULL) {
                ret = SET4dup(key, strlen(key), value, strlen(value), 0);
            }
            sets++;
        } else {
            // DEL
            DEL(key, strlen(key), free);
            dels++;
        }
    }

    double end = get_time_us();
    double elapsed_ms = (end - start) / 1000.0;
    double ops_per_sec = (num_ops / elapsed_ms) * 1000.0;

    printf("\n      Total operations: %d\n", num_ops);
    printf("      GET: %d, SET: %d, DEL: %d\n", gets, sets, dels);
    printf("      Time: %.2f ms\n", elapsed_ms);
    printf("      Throughput: %.2f ops/sec\n", ops_per_sec);

    ASSERT_GT(ops_per_sec, 20000, "Mixed workload throughput should be > 20K ops/sec");

    TEST_PASS();
}

// Test 4: Random access pattern (cache-unfriendly)
static void test_random_access_pattern(void) {
    TEST_START("Random access pattern");

    const int num_keys = 10000;
    const int num_ops = 50000;
    char key[64], value[128];

    // Pre-populate
    for (int i = 0; i < num_keys; i++) {
        generate_key(key, sizeof(key), i);
        generate_value(value, sizeof(value), i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        }
    }

    // Random access
    srand(12345); // Fixed seed for reproducibility
    double start = get_time_us();

    for (int i = 0; i < num_ops; i++) {
        int random_idx = rand() % num_keys;
        generate_key(key, sizeof(key), random_idx);
        osv *result = GET(key, strlen(key));
        ASSERT_NOT_NULL(result, "Random GET should find key");
    }

    double end = get_time_us();
    double elapsed_ms = (end - start) / 1000.0;
    double ops_per_sec = (num_ops / elapsed_ms) * 1000.0;

    printf("\n      Operations: %d (on %d keys)\n", num_ops, num_keys);
    printf("      Time: %.2f ms\n", elapsed_ms);
    printf("      Throughput: %.2f ops/sec\n", ops_per_sec);

    ASSERT_GT(ops_per_sec, 20000, "Random access should still be > 20K ops/sec");

    TEST_PASS();
}

// Test 5: Latency percentiles
static void test_latency_percentiles(void) {
    TEST_START("Latency percentiles (GET)");

    const int num_ops = 10000;
    double *latencies = malloc(sizeof(double) * num_ops);
    assert(latencies);

    char key[64], value[128];

    // Pre-populate
    for (int i = 0; i < num_ops; i++) {
        generate_key(key, sizeof(key), i);
        generate_value(value, sizeof(value), i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        }
    }

    // Measure individual GET latencies
    for (int i = 0; i < num_ops; i++) {
        generate_key(key, sizeof(key), i);
        double start = get_time_us();
        osv *result = GET(key, strlen(key));
        double end = get_time_us();
        ASSERT_NOT_NULL(result, "GET should succeed");
        latencies[i] = end - start;
    }

    // Sort latencies for percentile calculation
    for (int i = 0; i < num_ops - 1; i++) {
        for (int j = i + 1; j < num_ops; j++) {
            if (latencies[i] > latencies[j]) {
                double tmp = latencies[i];
                latencies[i] = latencies[j];
                latencies[j] = tmp;
            }
        }
    }

    double p50 = latencies[num_ops * 50 / 100];
    double p90 = latencies[num_ops * 90 / 100];
    double p99 = latencies[num_ops * 99 / 100];
    double p999 = latencies[num_ops * 999 / 1000];
    double max = latencies[num_ops - 1];

    printf("\n      P50:  %.3f μs\n", p50);
    printf("      P90:  %.3f μs\n", p90);
    printf("      P99:  %.3f μs\n", p99);
    printf("      P99.9: %.3f μs\n", p999);
    printf("      Max:  %.3f μs\n", max);

    free(latencies);

    ASSERT_LT(p99, 100.0, "P99 latency should be < 100 μs");

    TEST_PASS();
}

// Test 6: Scaling with data size
static void test_scaling_with_size(void) {
    TEST_START("Scaling with data size");

    const int num_sizes = 5;
    size_t sizes[] = {64, 256, 1024, 4096, 16384};
    const int ops_per_size = 10000;

    printf("\n");

    for (int s = 0; s < num_sizes; s++) {
        size_t value_size = sizes[s];
        char key[64];
        char *value = malloc(value_size);
        assert(value);
        memset(value, 'X', value_size);

        double start = get_time_us();

        for (int i = 0; i < ops_per_size; i++) {
            generate_key(key, sizeof(key), i);
            int ret = SET4dup(key, strlen(key), value, value_size, 0);
            if (ret == FULL) {
                ret = SET4dup(key, strlen(key), value, value_size, 0);
            }
        }

        double end = get_time_us();
        double elapsed_ms = (end - start) / 1000.0;
        double throughput_mb = (ops_per_size * value_size / (1024.0 * 1024.0)) / (elapsed_ms / 1000.0);

        printf("      Value size %5zu bytes: %.2f ms, %.2f MB/s\n",
               value_size, elapsed_ms, throughput_mb);

        free(value);

        // Cleanup
        for (int i = 0; i < ops_per_size; i++) {
            generate_key(key, sizeof(key), i);
            DEL(key, strlen(key), free);
        }
    }

    TEST_PASS();
}

// Test 7: Hash collision performance
static void test_collision_performance(void) {
    TEST_START("Hash collision performance");

    const int num_keys = 10000;
    char key[64], value[128];

    double start = get_time_us();

    // Insert keys that will cause collisions
    for (int i = 0; i < num_keys; i++) {
        snprintf(key, sizeof(key), "collision_%d", i);
        generate_value(value, sizeof(value), i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        }
    }

    double end = get_time_us();
    double elapsed_ms = (end - start) / 1000.0;
    double ops_per_sec = (num_keys / elapsed_ms) * 1000.0;

    printf("\n      Keys inserted: %d\n", num_keys);
    printf("      Time: %.2f ms\n", elapsed_ms);
    printf("      Throughput: %.2f ops/sec\n", ops_per_sec);
    printf("      Load factor: %.2f\n", (double) size / cap);

    // Performance should degrade gracefully with collisions
    ASSERT_GT(ops_per_sec, 5000, "Collision handling should maintain > 5K ops/sec");

    TEST_PASS();
}

// Test 8: Capacity expansion overhead
static void test_expansion_overhead(void) {
    TEST_START("Capacity expansion overhead");

    uint64_t initial_cap = cap;
    const int keys_per_batch = 1000;
    int total_expansions = 0;

    printf("\n      Initial capacity: %" PRIu64 "\n", initial_cap);

    char key[64], value[128];
    int total_keys = 0;

    // Insert until we see multiple expansions
    for (int batch = 0; batch < 10; batch++) {
        uint64_t cap_before = cap;

        double start = get_time_us();
        for (int i = 0; i < keys_per_batch; i++) {
            generate_key(key, sizeof(key), total_keys + i);
            generate_value(value, sizeof(value), total_keys + i);
            int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
            if (ret == FULL) {
                ret = expand_capacity(free);
                if (ret < 0) {
                    printf("expansion fail ");
                }
                ret = SET4dup(key, strlen(key), value, strlen(value), 0);
                if (ret < 0) {
                    printf("SET4dup fail ");
                }
            }
        }
        double end = get_time_us();
        double elapsed_ms = (end - start) / 1000.0;

        if (cap > cap_before) {
            total_expansions++;
            printf("      Expansion %d: %" PRIu64 " -> %" PRIu64 " (%.2f ms)\n",
                   total_expansions, cap_before, cap, elapsed_ms);
        }
        total_keys += keys_per_batch;
    }
    printf("      Total expansions: %d\n", total_expansions);
    printf("      Final capacity: %" PRIu64 "\n", cap);
    printf("      Final size: %" PRIu64 "\n", size);

    TEST_PASS();
}

// Test 9: Cache line utilization
static void test_cache_line_utilization(void) {
    TEST_START("Cache line utilization");

    // Verify structure size for optimal cache usage
    ASSERT_EQ(sizeof(ohash_t), 32, "ohash_t should be 32 bytes");

    // Calculate theoretical cache efficiency
    size_t cache_line_size = 64;
    size_t structs_per_line = cache_line_size / sizeof(ohash_t);

    printf("\n      Cache line size: %zu bytes\n", cache_line_size);
    printf("      ohash_t size: %zu bytes\n", sizeof(ohash_t));
    printf("      Structures per cache line: %zu\n", structs_per_line);
    printf("      Cache line utilization: %.1f%%\n",
           (structs_per_line * sizeof(ohash_t) * 100.0) / cache_line_size);

    ASSERT_EQ(structs_per_line, 2, "Should fit exactly 2 structs per cache line");

    TEST_PASS();
}

// Test 10: Expired entry overhead
static void test_expired_entry_overhead(void) {
    TEST_START("Expired entry overhead");

    const int num_keys = 5000;
    char key[64], value[128];
    uint32_t past_time = (uint32_t) get_current_time_seconds() - 100;

    // Insert expired entries
    for (int i = 0; i < num_keys; i++) {
        generate_key(key, sizeof(key), i);
        generate_value(value, sizeof(value), i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), past_time);
        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), value, strlen(value), past_time);
        }
    }

    // Measure GET performance on expired entries
    double start = get_time_us();

    for (int i = 0; i < num_keys; i++) {
        generate_key(key, sizeof(key), i);
        osv *result = GET(key, strlen(key));
        ASSERT_NULL(result, "Expired key should return NULL");
    }

    double end = get_time_us();
    double elapsed_ms = (end - start) / 1000.0;
    double ops_per_sec = (num_keys / elapsed_ms) * 1000.0;

    printf("\n      Expired keys: %d\n", num_keys);
    printf("      Time: %.2f ms\n", elapsed_ms);
    printf("      Throughput: %.2f ops/sec\n", ops_per_sec);

    // Expired entry detection should be fast
    ASSERT_GT(ops_per_sec, 50000, "Expired detection should be > 50K ops/sec");

    TEST_PASS();
}

// Test runner
void run_cmd_performance_tests(void) {
    TEST_SUITE_START("CMD + OHASH Performance Benchmarks");

    // Initialize with reasonable capacity
    int ret = initohash(1024);
    assert(ret == OK && "Hash table initialization failed");

    test_sequential_set_throughput();
    test_sequential_get_throughput();
    test_mixed_workload();
    test_random_access_pattern();
    test_latency_percentiles();
    test_scaling_with_size();
    test_collision_performance();
    test_expansion_overhead();
    test_cache_line_utilization();
    test_expired_entry_overhead();

    TEST_SUITE_END();
}
