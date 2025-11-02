//
// Created by weishen on 2025/11/2.
//

//
// test_ohash.c - Comprehensive Test Suite
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include "../storage/ohashtable.h"
#include "../command/cmd_.h"
#include "unistd.h"

// ============================================================================
// 测试工具宏和函数
// ============================================================================

#define TEST_START(name) \
    do { \
        printf("\n[TEST] %s ... ", name); \
        fflush(stdout); \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("✓ PASS\n"); \
        passed_tests++; \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        printf("✗ FAIL: %s\n", msg); \
        failed_tests++; \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("✗ FAIL: %s (expected %ld, got %ld)\n", msg, (long)(b), (long)(a)); \
            failed_tests++; \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while(0)

static int passed_tests = 0;
static int failed_tests = 0;

// 计时器
static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// 生成随机字符串
static char* random_string(size_t len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char *str = malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[len] = '\0';
    return str;
}

// ============================================================================
// 功能测试
// ============================================================================

void test_basic_set_get() {
    TEST_START("Basic SET and GET");

    char *key = "test_key";
    char *value = "test_value";

    int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
    ASSERT_EQ(ret, OK, "SET should return OK");

    osv *result = GET(key, strlen(key));
    ASSERT_TRUE(result != NULL, "GET should return non-NULL");
    ASSERT_TRUE(result->vlen == strlen(value), "Value length mismatch");
    ASSERT_TRUE(memcmp(result->d, value, strlen(value)) == 0, "Value content mismatch");

    TEST_PASS();
}

void test_set_replace() {
    TEST_START("SET with replacement");

    char *key = "replace_key";
    char *value1 = "value1";
    char *value2 = "value2_longer";

    int ret1 = SET4dup(key, strlen(key), value1, strlen(value1), 0, free);
    ASSERT_EQ(ret1, OK, "First SET should return OK");

    int ret2 = SET4dup(key, strlen(key), value2, strlen(value2), 0, free);
    ASSERT_EQ(ret2, REPLACED, "Second SET should return REPLACED");

    osv *result = GET(key, strlen(key));
    ASSERT_TRUE(result != NULL, "GET should return non-NULL");
    ASSERT_TRUE(result->vlen == strlen(value2), "Value should be updated");
    ASSERT_TRUE(memcmp(result->d, value2, strlen(value2)) == 0, "Value content should be updated");

    TEST_PASS();
}

void test_del() {
    TEST_START("DEL operation");

    char *key = "del_key";
    char *value = "del_value";

    SET4dup(key, strlen(key), value, strlen(value), 0, free);

    osv *before = GET(key, strlen(key));
    ASSERT_TRUE(before != NULL, "Key should exist before DEL");

    int ret = DEL(key, strlen(key), free);
    ASSERT_EQ(ret, 0, "DEL should return 0");

    osv *after = GET(key, strlen(key));
    ASSERT_TRUE(after == NULL, "Key should not exist after DEL");

    TEST_PASS();
}

void test_expiration() {
    TEST_START("Expiration functionality");

    char *key = "expire_key";
    char *value = "expire_value";
    uint32_t expire_time = (uint32_t)time(NULL) + 2; // 2秒后过期

    SET4dup(key, strlen(key), value, strlen(value), expire_time, free);

    osv *before = GET(key, strlen(key));
    ASSERT_TRUE(before != NULL, "Key should exist before expiration");

    sleep(3); // 等待过期

    osv *after = GET(key, strlen(key));
    ASSERT_TRUE(after == NULL, "Key should not exist after expiration");

    TEST_PASS();
}

void test_update_expiration() {
    TEST_START("Update expiration time");

    char *key = "update_expire_key";
    char *value = "value";
    uint32_t expire_time1 = (uint32_t)time(NULL) + 1;

    SET4dup(key, strlen(key), value, strlen(value), expire_time1, free);

    uint32_t expire_time2 = (uint32_t)time(NULL) + 10; // 延长过期时间
    EXPIRED(key, strlen(key), expire_time2);

    sleep(2); // 原本应该过期了

    osv *result = GET(key, strlen(key));
    ASSERT_TRUE(result != NULL, "Key should still exist after updating expiration");

    TEST_PASS();
}

void test_boundary_conditions() {
    TEST_START("Boundary conditions");

    // 空key测试（应该被拒绝）
    osv *empty_key_result = GET("", 0);
    ASSERT_TRUE(empty_key_result == NULL, "Empty key should be rejected");

    // 最大长度key
    char *max_key = malloc(MAX_KEY_LEN + 1);
    memset(max_key, 'a', MAX_KEY_LEN);
    max_key[MAX_KEY_LEN] = '\0';
    char *value = "value";

    int ret = SET4dup(max_key, MAX_KEY_LEN, value, strlen(value), 0, free);
    ASSERT_EQ(ret, OK, "Max length key should be accepted");

    // 超过最大长度（应该被拒绝）
    int invalid_ret = SET4dup(max_key, MAX_KEY_LEN + 1, value, strlen(value), 0, free);
    ASSERT_TRUE(invalid_ret == -EINVAL, "Over-max length key should be rejected");

    free(max_key);
    TEST_PASS();
}

void test_collision_handling() {
    TEST_START("Hash collision handling");

    // 插入多个可能冲突的key
    for (int i = 0; i < 100; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "collision_key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        if (ret == FULL) {
            // 触发扩容
            expand_capacity(free);
            ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        }
        ASSERT_TRUE(ret == OK || ret == FULL, "SET should succeed or return FULL");
    }

    // 验证所有key都能正确取出
    for (int i = 0; i < 100; i++) {
        char key[32], expected_value[32];
        snprintf(key, sizeof(key), "collision_key_%d", i);
        snprintf(expected_value, sizeof(expected_value), "value_%d", i);

        osv *result = GET(key, strlen(key));
        ASSERT_TRUE(result != NULL, "All keys should be retrievable");
        ASSERT_TRUE(memcmp(result->d, expected_value, strlen(expected_value)) == 0,
                   "Value should match");
    }

    TEST_PASS();
}

void test_expansion() {
    TEST_START("Table expansion");

    uint64_t initial_cap = cap;

    // 插入足够多的数据触发扩容
    int count = 0;
    while (cap == initial_cap && count < 10000) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "expand_key_%d", count);
        snprintf(value, sizeof(value), "value_%d", count);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        if (ret == FULL) {
            break;
        }
        count++;
    }

    // 手动扩容
    int expand_ret = expand_capacity(free);
    ASSERT_EQ(expand_ret, OK, "Expansion should succeed");

    // 验证数据完整性
    for (int i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "expand_key_%d", i);
        osv *result = GET(key, strlen(key));
        ASSERT_TRUE(result != NULL, "Data should survive expansion");
    }

    TEST_PASS();
}

void test_large_values() {
    TEST_START("Large value handling");

    char *key = "large_key";
    size_t large_size = 1024 * 1024; // 1MB
    char *large_value = malloc(large_size);
    memset(large_value, 'X', large_size);

    int ret = SET4dup(key, strlen(key), large_value, large_size, 0, free);
    ASSERT_EQ(ret, OK, "Should handle large values");

    osv *result = GET(key, strlen(key));
    ASSERT_TRUE(result != NULL, "Should retrieve large value");
    ASSERT_TRUE(result->vlen == large_size, "Large value size should match");

    free(large_value);
    TEST_PASS();
}

// ============================================================================
// 性能测试
// ============================================================================

void perf_test_insert(int num_items) {
    printf("\n[PERF] Insert Performance (%d items)\n", num_items);

    double start = get_time_ms();

    for (int i = 0; i < num_items; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "perf_key_%d", i);
        snprintf(value, sizeof(value), "perf_value_%d", i);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        if (ret == FULL) {
            expand_capacity(free);
            SET4dup(key, strlen(key), value, strlen(value), 0, free);
        }
    }

    double end = get_time_ms();
    double elapsed = end - start;

    printf("  Total time: %.2f ms\n", elapsed);
    printf("  Throughput: %.0f ops/sec\n", (num_items / elapsed) * 1000);
    printf("  Avg latency: %.3f μs/op\n", (elapsed * 1000) / num_items);
    printf("  Final capacity: %lu\n", cap);
    printf("  Load factor: %.2f\n", (double)size / cap);
}

void perf_test_get(int num_items) {
    printf("\n[PERF] GET Performance (%d items)\n", num_items);

    // 先插入数据
    for (int i = 0; i < num_items; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "get_key_%d", i);
        snprintf(value, sizeof(value), "get_value_%d", i);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        if (ret == FULL) {
            expand_capacity(free);
            SET4dup(key, strlen(key), value, strlen(value), 0, free);
        }
    }

    // 测试GET性能
    double start = get_time_ms();

    for (int i = 0; i < num_items; i++) {
        char key[32];
        snprintf(key, sizeof(key), "get_key_%d", i);
        osv *result = GET(key, strlen(key));
        assert(result != NULL); // 不应该为空
    }

    double end = get_time_ms();
    double elapsed = end - start;

    printf("  Total time: %.2f ms\n", elapsed);
    printf("  Throughput: %.0f ops/sec\n", (num_items / elapsed) * 1000);
    printf("  Avg latency: %.3f μs/op\n", (elapsed * 1000) / num_items);
}

void perf_test_random_access(int num_items, int num_ops) {
    printf("\n[PERF] Random Access (%d items, %d operations)\n", num_items, num_ops);

    // 插入数据
    for (int i = 0; i < num_items; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "rand_key_%d", i);
        snprintf(value, sizeof(value), "rand_value_%d", i);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        if (ret == FULL) {
            expand_capacity(free);
            SET4dup(key, strlen(key), value, strlen(value), 0, free);
        }
    }

    // 随机访问
    double start = get_time_ms();
    int hits = 0;

    for (int i = 0; i < num_ops; i++) {
        int idx = rand() % num_items;
        char key[32];
        snprintf(key, sizeof(key), "rand_key_%d", idx);

        osv *result = GET(key, strlen(key));
        if (result != NULL) hits++;
    }

    double end = get_time_ms();
    double elapsed = end - start;

    printf("  Total time: %.2f ms\n", elapsed);
    printf("  Throughput: %.0f ops/sec\n", (num_ops / elapsed) * 1000);
    printf("  Hit rate: %.2f%%\n", (hits * 100.0) / num_ops);
}

void perf_test_mixed_workload(int num_ops) {
    printf("\n[PERF] Mixed Workload (%d operations)\n", num_ops);

    double start = get_time_ms();

    int sets = 0, gets = 0, dels = 0;

    for (int i = 0; i < num_ops; i++) {
        int op = rand() % 100;
        char key[32], value[64];
        snprintf(key, sizeof(key), "mixed_key_%d", rand() % 10000);
        snprintf(value, sizeof(value), "value_%d", i);

        if (op < 50) { // 50% GET
            GET(key, strlen(key));
            gets++;
        } else if (op < 90) { // 40% SET
            int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
            if (ret == FULL) {
                expand_capacity(free);
                SET4dup(key, strlen(key), value, strlen(value), 0, free);
            }
            sets++;
        } else { // 10% DEL
            DEL(key, strlen(key), free);
            dels++;
        }
    }

    double end = get_time_ms();
    double elapsed = end - start;

    printf("  Total time: %.2f ms\n", elapsed);
    printf("  Throughput: %.0f ops/sec\n", (num_ops / elapsed) * 1000);
    printf("  Operations: SET=%d, GET=%d, DEL=%d\n", sets, gets, dels);
}

// ============================================================================
// 压力测试
// ============================================================================

void stress_test_high_load() {
    printf("\n[STRESS] High load factor test\n");

    // 填充到接近负载因子阈值
    int target_size = (cap * LOAD_FACTOR_THRESHOLD) / LOAD_FACTOR_DENOMINATOR;

    for (int i = 0; i < target_size; i++) {
        char *key = random_string(20);
        char *value = random_string(50);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
        if (ret == FULL) {
            expand_capacity(free);
            SET4dup(key, strlen(key), value, strlen(value), 0, free);
        }

        free(key);
        free(value);
    }

    printf("  Filled to size: %lu\n", size);
    printf("  Capacity: %lu\n", cap);
    printf("  Load factor: %.2f\n", (double)size / cap);
    printf("  ✓ Stress test completed\n");
}

void stress_test_rapid_churn() {
    printf("\n[STRESS] Rapid churn test (insert/delete cycles)\n");

    for (int cycle = 0; cycle < 5; cycle++) {
        // 快速插入
        for (int i = 0; i < 1000; i++) {
            char key[32], value[64];
            snprintf(key, sizeof(key), "churn_%d", i);
            snprintf(value, sizeof(value), "value_%d", i);

            int ret = SET4dup(key, strlen(key), value, strlen(value), 0, free);
            if (ret == FULL) {
                expand_capacity(free);
                SET4dup(key, strlen(key), value, strlen(value), 0, free);
            }
        }

        // 快速删除
        for (int i = 0; i < 1000; i++) {
            char key[32];
            snprintf(key, sizeof(key), "churn_%d", i);
            DEL(key, strlen(key), free);
        }

        printf("  Cycle %d completed (size: %lu)\n", cycle + 1, size);
    }

    printf("  ✓ Churn test completed\n");
}

// ============================================================================
// 主测试入口
// ============================================================================

void run_functional_tests() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("              FUNCTIONAL TESTS\n");
    printf("═══════════════════════════════════════════════════════════\n");

    test_basic_set_get();
    test_set_replace();
    test_del();
    test_expiration();
    test_update_expiration();
    test_boundary_conditions();
    test_collision_handling();
    test_expansion();
    test_large_values();
}

void run_performance_tests() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("              PERFORMANCE TESTS\n");
    printf("═══════════════════════════════════════════════════════════\n");

    perf_test_insert(10000);
    perf_test_get(10000);
    perf_test_random_access(10000, 100000);
    perf_test_mixed_workload(50000);
}

void run_stress_tests() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("              STRESS TESTS\n");
    printf("═══════════════════════════════════════════════════════════\n");

    stress_test_high_load();
    stress_test_rapid_churn();
}

int main(int argc, char *argv[]) {
    srand(time(NULL)); //expand_key_285

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║         OHASH COMPREHENSIVE TEST SUITE                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    // 初始化哈希表
    if (initohash(1024) != OK) {
        fprintf(stderr, "Failed to initialize hash table\n");
        return 1;
    }

    printf("\nInitial capacity: %lu\n", cap);

    // 运行测试
    run_functional_tests();
    // run_performance_tests();
    // run_stress_tests();

    // 测试总结
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("              TEST SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  Total tests: %d\n", passed_tests + failed_tests);
    printf("  ✓ Passed: %d\n", passed_tests);
    printf("  ✗ Failed: %d\n", failed_tests);
    printf("  Success rate: %.1f%%\n",
           (passed_tests * 100.0) / (passed_tests + failed_tests));
    printf("═══════════════════════════════════════════════════════════\n");

    return failed_tests > 0 ? 1 : 0;
}