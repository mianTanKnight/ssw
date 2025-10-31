//
// Comprehensive Test Suite for ohashtable v2
// Tests: Functionality, Edge Cases, Performance, Memory Safety
//
// New Design Features:
// - rm bit: tracks ownership (rm=0: table owns, rm=1: returned)
// - Manual expansion: expand_capacity(free_func) must be called explicitly
// - oinsert returns FULL when load factor reached (no auto-expand)
//

#include "../storage/ohashtable.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

// ============================================================================
// Test Framework Macros
// ============================================================================

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_RESET   "\x1b[0m"

static int g_test_count = 0;
static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_START(name) \
    do { \
        printf(COLOR_BLUE "[ RUN      ] %s" COLOR_RESET "\n", name); \
        g_test_count++; \
    } while(0)

#define TEST_PASS(name) \
    do { \
        printf(COLOR_GREEN "[       OK ] %s" COLOR_RESET "\n", name); \
        g_test_passed++; \
    } while(0)

#define TEST_FAIL(name, msg) \
    do { \
        printf(COLOR_RED "[  FAILED  ] %s: %s" COLOR_RESET "\n", name, msg); \
        g_test_failed++; \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf(COLOR_RED "  ASSERT_EQ failed: %s\n  Expected: %ld, Got: %ld" COLOR_RESET "\n", \
                   msg, (long)(b), (long)(a)); \
            return -1; \
        } \
    } while(0)

#define ASSERT_NE(a, b, msg) \
    do { \
        if ((a) == (b)) { \
            printf(COLOR_RED "  ASSERT_NE failed: %s" COLOR_RESET "\n", msg); \
            return -1; \
        } \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            printf(COLOR_RED "  ASSERT_TRUE failed: %s" COLOR_RESET "\n", msg); \
            return -1; \
        } \
    } while(0)

#define ASSERT_NULL(ptr, msg) \
    do { \
        if ((ptr) != NULL) { \
            printf(COLOR_RED "  ASSERT_NULL failed: %s" COLOR_RESET "\n", msg); \
            return -1; \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr, msg) \
    do { \
        if ((ptr) == NULL) { \
            printf(COLOR_RED "  ASSERT_NOT_NULL failed: %s" COLOR_RESET "\n", msg); \
            return -1; \
        } \
    } while(0)

// ============================================================================
// Performance Measurement
// ============================================================================

static inline double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

#define PERF_START() double __start_time = get_time_ms()
#define PERF_END(ops) \
    do { \
        double __end_time = get_time_ms(); \
        double __elapsed = __end_time - __start_time; \
        double __ops_per_sec = (ops) / (__elapsed / 1000.0); \
        printf("  ⏱  Time: %.2f ms, Throughput: %.0f ops/sec\n", \
               __elapsed, __ops_per_sec); \
    } while(0)

// ============================================================================
// Test Utilities
// ============================================================================

// Cleanup function that respects rm bit
static void cleanup_table(void) {
    if (ohashtabl) {
        for (uint64_t i = 0; i < cap; i++) {
            // Only free elements where table still owns (rm=0)
            if (ohashtabl[i].key && !ohashtabl[i].rm) {
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

static char *make_key(const char *prefix, int num) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s_%d", prefix, num);
    return strdup(buf);
}

static int *make_value(int val) {
    int *p = malloc(sizeof(int));
    *p = val;
    return p;
}

// Helper to handle insert with potential expansion
static int insert_with_expand(char *key, uint32_t keylen, void *v,
                               int expira, oret_t *oret) {
    int ret = oinsert(key, keylen, v, expira, oret);

    if (ret == FULL) {
        // Need to expand
        if (expand_capacity(free) < 0) {
            return -ENOMEM;
        }
        // Retry insert
        ret = oinsert(key, keylen, v, expira, oret);
    }

    return ret;
}

// ============================================================================
// Basic Functionality Tests
// ============================================================================

int test_init_and_destroy(void) {
    TEST_START("test_init_and_destroy");

    // Test power-of-2 initialization
    ASSERT_EQ(initohash(100), OK, "Init with non-power-of-2");
    ASSERT_TRUE(cap >= 100 && (cap & (cap - 1)) == 0, "Capacity should be power of 2");
    cleanup_table();

    // Test exact power-of-2
    ASSERT_EQ(initohash(128), OK, "Init with power-of-2");
    ASSERT_EQ(cap, 128, "Capacity should be 128");
    cleanup_table();

    TEST_PASS("test_init_and_destroy");
    return 0;
}

int test_basic_insert_get(void) {
    TEST_START("test_basic_insert_get");

    initohash(16);

    char *key = make_key("test", 1);
    int *value = make_value(42);

    ASSERT_EQ(oinsert(key, strlen(key), value, 0, NULL), OK, "Insert should succeed");
    ASSERT_EQ(size, 1, "Size should be 1");

    int *result = oget("test_1", strlen("test_1"));
    ASSERT_NOT_NULL(result, "Get should return value");
    ASSERT_EQ(*result, 42, "Value should be 42");

    cleanup_table();
    TEST_PASS("test_basic_insert_get");
    return 0;
}

int test_insert_replace(void) {
    TEST_START("test_insert_replace");

    initohash(16);

    char *key1 = make_key("test", 1);
    int *value1 = make_value(42);
    ASSERT_EQ(oinsert(key1, strlen(key1), value1, 0, NULL), OK, "First insert");

    // Replace with new value
    char *key2 = make_key("test", 1);
    int *value2 = make_value(100);
    oret_t old = {0};
    ASSERT_EQ(oinsert(key2, strlen(key2), value2, 0, &old), REPLACED, "Should return REPLACED");

    // Check returned values
    ASSERT_EQ(old.key, key2, "Returned key should be new key (rejected)");
    ASSERT_EQ(*(int*)old.value, 42, "Old value should be returned");

    // Cleanup returned ownership
    if (old.key) free(old.key);
    if (old.value) free(old.value);

    // Verify new value
    int *result = oget("test_1", strlen("test_1"));
    ASSERT_EQ(*result, 100, "Value should be replaced to 100");

    cleanup_table();
    TEST_PASS("test_insert_replace");
    return 0;
}

int test_take_ownership(void) {
    TEST_START("test_take_ownership");

    initohash(16);

    char *key = make_key("test", 1);
    int *value = make_value(42);
    oinsert(key, strlen(key), value, 0, NULL);

    oret_t ret = {0};
    otake("test_1", strlen("test_1"), &ret);

    ASSERT_NOT_NULL(ret.key, "Key should be returned");
    ASSERT_NOT_NULL(ret.value, "Value should be returned");
    ASSERT_EQ(*(int*)ret.value, 42, "Value should be 42");
    ASSERT_EQ(size, 0, "Size should be 0 after take");

    // Verify tombstone
    void *result = oget("test_1", strlen("test_1"));
    ASSERT_NULL(result, "Should not find deleted key");

    // Cleanup returned ownership
    if (ret.key) free(ret.key);
    if (ret.value) free(ret.value);

    cleanup_table();
    TEST_PASS("test_take_ownership");
    return 0;
}

int test_rm_bit_semantics(void) {
    TEST_START("test_rm_bit_semantics");

    initohash(16);

    // Insert - should set rm=0 (table owns)
    char *key = make_key("test", 1);
    int *value = make_value(42);
    oinsert(key, strlen(key), value, 0, NULL);

    // Find the slot
    uint64_t hash = XXH64(key, strlen(key), H_SEED);
    uint64_t idx = hash & (cap - 1);
    while (ohashtabl[idx].key == NULL || strcmp(ohashtabl[idx].key, key) != 0) {
        idx = (idx + 1) & (cap - 1);
    }

    ASSERT_EQ(ohashtabl[idx].rm, 0, "After insert, rm should be 0");

    // Take - should set rm=1 (ownership returned)
    oret_t ret = {0};
    otake("test_1", strlen("test_1"), &ret);

    ASSERT_EQ(ohashtabl[idx].rm, 1, "After take, rm should be 1");
    ASSERT_EQ(ohashtabl[idx].tb, 1, "After take, tb should be 1");

    // Cleanup
    if (ret.key) free(ret.key);
    if (ret.value) free(ret.value);

    cleanup_table();
    TEST_PASS("test_rm_bit_semantics");
    return 0;
}

// ============================================================================
// Tombstone and Probing Chain Tests
// ============================================================================

int test_tombstone_probing(void) {
    TEST_START("test_tombstone_probing");

    initohash(16);

    char *keys[3];
    int *values[3];

    for (int i = 0; i < 3; i++) {
        keys[i] = make_key("key", i);
        values[i] = make_value(i * 10);
        oinsert(keys[i], strlen(keys[i]), values[i], 0, NULL);
    }

    // Delete middle one
    oret_t ret = {0};
    otake(keys[1], strlen(keys[1]), &ret);
    if (ret.key) free(ret.key);
    if (ret.value) free(ret.value);

    // Should still find the last one (probing chain intact)
    int *result = oget(keys[2], strlen(keys[2]));
    ASSERT_NOT_NULL(result, "Should find key after tombstone");
    ASSERT_EQ(*result, 20, "Value should be correct");

    // Can insert into tombstone slot
    char *new_key = strdup("newkey");
    int *new_val = make_value(999);
    int insert_result = oinsert(new_key, strlen(new_key), new_val, 0, NULL);
    ASSERT_TRUE(insert_result == OK || insert_result == REPLACED,
                "Should be able to insert");

    cleanup_table();
    TEST_PASS("test_tombstone_probing");
    return 0;
}

// ============================================================================
// Expiration Tests
// ============================================================================

int test_expiration(void) {
    TEST_START("test_expiration");

    initohash(16);

    char *key = make_key("expire", 1);
    int *value = make_value(42);
    long now = get_current_time_seconds();

    // Insert with 1 second expiration
    oinsert(key, strlen(key), value, now + 1, NULL);

    // Should be accessible now
    int *result = oget("expire_1", strlen("expire_1"));
    ASSERT_NOT_NULL(result, "Should find non-expired key");

    // Sleep 2 seconds
    sleep(2);

    // Should be expired now
    result = oget("expire_1", strlen("expire_1"));
    ASSERT_NULL(result, "Should not find expired key");

    cleanup_table();
    TEST_PASS("test_expiration");
    return 0;
}

int test_lazy_expiration_cleanup(void) {
    TEST_START("test_lazy_expiration_cleanup");

    initohash(16);

    long now = get_current_time_seconds();

    // Insert multiple keys with expiration
    for (int i = 0; i < 5; i++) {
        char *key = make_key("expire", i);
        int *value = make_value(i);
        oinsert(key, strlen(key), value, now + 1, NULL);
    }

    ASSERT_EQ(size, 5, "Should have 5 entries");

    sleep(2);

    // Access one key - should trigger lazy cleanup
    void *result = oget("expire_0", strlen("expire_0"));
    ASSERT_NULL(result, "Expired key should not be found");

    cleanup_table();
    TEST_PASS("test_lazy_expiration_cleanup");
    return 0;
}

// ============================================================================
// Manual Expansion Tests
// ============================================================================

int test_manual_expansion(void) {
    TEST_START("test_manual_expansion");

    initohash(8);
    uint64_t initial_cap = cap;

    // Fill to capacity (load factor 0.7)
    // 8 * 0.7 = 5.6, so 6 inserts will hit limit
    int inserted = 0;
    for (int i = 0; i < 6; i++) {
        char *key = make_key("expand", i);
        int *value = make_value(i);
        int ret = oinsert(key, strlen(key), value, 0, NULL);
        if (ret == OK) {
            inserted++;
        } else if (ret == FULL) {
            // Expected - hit load factor
            free(key);
            free(value);
            break;
        }
    }

    printf("  Inserted %d elements before FULL\n", inserted);

    // Manually expand
    ASSERT_EQ(expand_capacity(free), OK, "Expansion should succeed");
    ASSERT_TRUE(cap > initial_cap, "Capacity should have doubled");

    // Now should be able to insert more
    for (int i = 6; i < 10; i++) {
        char *key = make_key("expand", i);
        int *value = make_value(i);
        int ret = oinsert(key, strlen(key), value, 0, NULL);
        ASSERT_EQ(ret, OK, "Should insert after expansion");
    }

    cleanup_table();
    TEST_PASS("test_manual_expansion");
    return 0;
}

int test_expansion_with_tombstones(void) {
    TEST_START("test_expansion_with_tombstones");

    initohash(8);

    // Insert some elements
    for (int i = 0; i < 4; i++) {
        char *key = make_key("temp", i);
        int *value = make_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    // Delete half (creates tombstones with rm=1)
    for (int i = 0; i < 2; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "temp_%d", i);
        oret_t ret = {0};
        otake(key_buf, strlen(key_buf), &ret);
        if (ret.key) free(ret.key);
        if (ret.value) free(ret.value);
    }

    ASSERT_EQ(size, 2, "Should have 2 live entries");

    // Expand - should only migrate live entries (rm=0)
    ASSERT_EQ(expand_capacity(free), OK, "Expansion should succeed");

    // Verify remaining keys are accessible
    for (int i = 2; i < 4; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "temp_%d", i);
        int *result = oget(key_buf, strlen(key_buf));
        ASSERT_NOT_NULL(result, "Surviving keys should be accessible");
        ASSERT_EQ(*result, i, "Values should be correct");
    }

    // Verify deleted keys are not accessible
    for (int i = 0; i < 2; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "temp_%d", i);
        int *result = oget(key_buf, strlen(key_buf));
        ASSERT_NULL(result, "Deleted keys should not be found");
    }

    cleanup_table();
    TEST_PASS("test_expansion_with_tombstones");
    return 0;
}

int test_expansion_with_expired(void) {
    TEST_START("test_expansion_with_expired");

    initohash(8);
    long now = get_current_time_seconds();

    // Insert with short expiration
    for (int i = 0; i < 4; i++) {
        char *key = make_key("expire", i);
        int *value = make_value(i);
        oinsert(key, strlen(key), value, now + 1, NULL);
    }

    sleep(2);

    printf("  Elements expired, now expanding...\n");

    // Expand - should clean up expired elements (rm=0, tb=1)
    ASSERT_EQ(expand_capacity(free), OK, "Expansion should succeed");

    // Verify expired keys are not in new table
    for (int i = 0; i < 4; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "expire_%d", i);
        int *result = oget(key_buf, strlen(key_buf));
        ASSERT_NULL(result, "Expired keys should not be found");
    }

    cleanup_table();
    TEST_PASS("test_expansion_with_expired");
    return 0;
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

int test_empty_key(void) {
    TEST_START("test_empty_key");

    initohash(16);

    char *key = strdup("");
    int *value = make_value(42);

    ASSERT_EQ(oinsert(key, 0, value, 0, NULL), OK, "Empty key should work");

    int *result = oget("", 0);
    ASSERT_NOT_NULL(result, "Should find empty key");
    ASSERT_EQ(*result, 42, "Value should be correct");

    cleanup_table();
    TEST_PASS("test_empty_key");
    return 0;
}

int test_large_key(void) {
    TEST_START("test_large_key");

    initohash(16);

    // 1KB key
    char *key = malloc(1024);
    memset(key, 'A', 1023);
    key[1023] = '\0';
    int *value = make_value(42);

    ASSERT_EQ(oinsert(key, 1023, value, 0, NULL), OK, "Large key should work");

    int *result = oget(key, 1023);
    ASSERT_NOT_NULL(result, "Should find large key");
    ASSERT_EQ(*result, 42, "Value should be correct");

    cleanup_table();
    TEST_PASS("test_large_key");
    return 0;
}

int test_collision_stress(void) {
    TEST_START("test_collision_stress");

    initohash(1024);  // Large enough to avoid FULL

    // Insert many keys to stress collision handling
    const int N = 1000;
    for (int i = 0; i < N; i++) {
        char *key = make_key("stress", i);
        int *value = make_value(i);
        int ret = insert_with_expand(key, strlen(key), value, 0, NULL);
        ASSERT_TRUE(ret == OK || ret == REPLACED, "Insert should succeed or replace");
    }

    // Verify all keys
    int found = 0;
    for (int i = 0; i < N; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "stress_%d", i);
        int *result = oget(key_buf, strlen(key_buf));
        if (result && *result == i) found++;
    }

    ASSERT_EQ(found, N, "All keys should be found");

    cleanup_table();
    TEST_PASS("test_collision_stress");
    return 0;
}

int test_interleaved_operations(void) {
    TEST_START("test_interleaved_operations");

    initohash(128);  // Large enough

    // Interleave insert, get, take operations
    for (int i = 0; i < 100; i++) {
        char *key = make_key("inter", i);
        int *value = make_value(i);
        oret_t ret = {0};
        int s = insert_with_expand(key, strlen(key), value, 0, &ret);

        if (s == REPLACED) {
            if (ret.key) free(ret.key);
            if (ret.value) free(ret.value);
        } else if (s < 0 && s != FULL) {
            free(key);
            free(value);
        }

        if (i % 3 == 0) {
            // Get
            char key_buf[32];
            snprintf(key_buf, sizeof(key_buf), "inter_%d", i);
            oget(key_buf, strlen(key_buf));
        }

        if (i % 5 == 0 && i > 0) {
            // Take
            char key_buf[32];
            snprintf(key_buf, sizeof(key_buf), "inter_%d", i - 1);
            oret_t take_ret = {0};
            otake(key_buf, strlen(key_buf), &take_ret);
            if (take_ret.key) free(take_ret.key);
            if (take_ret.value) free(take_ret.value);
        }
    }

    ASSERT_TRUE(size > 0, "Should have elements remaining");

    cleanup_table();
    TEST_PASS("test_interleaved_operations");
    return 0;
}

// ============================================================================
// Performance Tests
// ============================================================================

int test_insert_performance(void) {
    TEST_START("test_insert_performance");

    initohash(200000);  // Large enough to avoid expansions

    const int N = 100000;
    printf("  Inserting %d elements...\n", N);

    PERF_START();
    for (int i = 0; i < N; i++) {
        char *key = make_key("perf", i);
        int *value = make_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }
    PERF_END(N);

    cleanup_table();
    TEST_PASS("test_insert_performance");
    return 0;
}

int test_get_performance(void) {
    TEST_START("test_get_performance");

    initohash(200000);

    const int N = 100000;

    // Setup
    for (int i = 0; i < N; i++) {
        char *key = make_key("perf", i);
        int *value = make_value(i);
        oinsert(key, strlen(key), value, 0, NULL);
    }

    printf("  Getting %d elements...\n", N);

    PERF_START();
    for (int i = 0; i < N; i++) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "perf_%d", i);
        oget(key_buf, strlen(key_buf));
    }
    PERF_END(N);

    cleanup_table();
    TEST_PASS("test_get_performance");
    return 0;
}

int test_mixed_workload_performance(void) {
    TEST_START("test_mixed_workload_performance");

    initohash(100000);  // Large enough

    const int N = 50000;
    printf("  Mixed workload: %d ops (50%% insert, 40%% get, 10%% delete)...\n", N);

    PERF_START();
    for (int i = 0; i < N; i++) {
        int op = i % 10;

        if (op < 5) {
            // Insert
            char *key = make_key("mixed", i);
            int *value = make_value(i);
            oret_t ret = {0};
            int s = oinsert(key, strlen(key), value, 0, &ret);

            if (s == REPLACED) {
                // Table rejected new key, returned old value
                if (ret.key) free(ret.key);
                if (ret.value) free(ret.value);
            } else if (s < 0 && s != FULL) {
                // Error - clean up
                free(key);
                free(value);
            }
            // If s == OK, ownership transferred

        } else if (op < 9) {
            // Get
            char key_buf[32];
            snprintf(key_buf, sizeof(key_buf), "mixed_%d", i / 2);
            oget(key_buf, strlen(key_buf));

        } else {
            // Delete
            char key_buf[32];
            snprintf(key_buf, sizeof(key_buf), "mixed_%d", i / 2);
            oret_t ret = {0};
            otake(key_buf, strlen(key_buf), &ret);
            if (ret.key) free(ret.key);
            if (ret.value) free(ret.value);
        }
    }
    PERF_END(N);

    cleanup_table();
    TEST_PASS("test_mixed_workload_performance");
    return 0;
}

int test_cache_friendliness(void) {
    TEST_START("test_cache_friendliness");

    initohash(1024);

    printf("  Testing 32-byte struct alignment...\n");
    printf("  sizeof(ohash_t) = %zu bytes\n", sizeof(ohash_t));
    ASSERT_EQ(sizeof(ohash_t), 32, "ohash_t should be exactly 32 bytes");

    // Verify alignment
    ASSERT_EQ((uintptr_t)&ohashtabl[0] % 8, 0, "Should be 8-byte aligned");

    printf("  ✓ Cache line optimized: 64 bytes = 2 slots\n");

    cleanup_table();
    TEST_PASS("test_cache_friendliness");
    return 0;
}

// ============================================================================
// Memory Safety Tests
// ============================================================================

int test_no_double_free(void) {
    TEST_START("test_no_double_free");

    initohash(16);

    char *key = make_key("test", 1);
    int *value = make_value(42);
    oinsert(key, strlen(key), value, 0, NULL);

    // Take once
    oret_t ret = {0};
    otake("test_1", strlen("test_1"), &ret);
    ASSERT_NOT_NULL(ret.key, "Should get ownership back");

    // Take again - should return nothing (tombstone with rm=1)
    oret_t ret2 = {0};
    otake("test_1", strlen("test_1"), &ret2);
    ASSERT_NULL(ret2.key, "Should not return already-taken key");

    // Cleanup first take
    if (ret.key) free(ret.key);
    if (ret.value) free(ret.value);

    cleanup_table();
    TEST_PASS("test_no_double_free");
    return 0;
}

int test_expansion_memory_safety(void) {
    TEST_START("test_expansion_memory_safety");

    initohash(8);
    long now = get_current_time_seconds();

    // Create mixed state:
    // - Some live (rm=0, tb=0)
    // - Some taken (rm=1, tb=1)
    // - Some expired (rm=0, tb=1)

    // Insert 4 elements
    for (int i = 0; i < 4; i++) {
        char *key = make_key("mem", i);
        int *value = make_value(i);
        oinsert(key, strlen(key), value, i % 2 ? 0 : now + 1, NULL);
    }

    // Take element 0 (rm=1)
    oret_t ret = {0};
    otake("mem_0", strlen("mem_0"), &ret);
    if (ret.key) free(ret.key);
    if (ret.value) free(ret.value);

    // Wait for expiration of element 2
    sleep(2);

    printf("  State before expansion:\n");
    printf("    mem_0: taken (rm=1)\n");
    printf("    mem_1: live (rm=0)\n");
    printf("    mem_2: expired (rm=0, tb=1 after access)\n");
    printf("    mem_3: live (rm=0)\n");

    // Trigger lazy deletion of expired
    oget("mem_2", strlen("mem_2"));

    // Expand - should properly handle all states
    ASSERT_EQ(expand_capacity(free), OK, "Expansion should succeed");

    // Verify live elements survived
    int *r1 = oget("mem_1", strlen("mem_1"));
    ASSERT_NOT_NULL(r1, "Live element should survive");
    ASSERT_EQ(*r1, 1, "Value should be correct");

    int *r3 = oget("mem_3", strlen("mem_3"));
    ASSERT_NOT_NULL(r3, "Live element should survive");
    ASSERT_EQ(*r3, 3, "Value should be correct");

    // Verify taken/expired are gone
    ASSERT_NULL(oget("mem_0", strlen("mem_0")), "Taken should be gone");
    ASSERT_NULL(oget("mem_2", strlen("mem_2")), "Expired should be gone");

    cleanup_table();
    TEST_PASS("test_expansion_memory_safety");
    return 0;
}

// ============================================================================
// Main Test Runner
// ============================================================================

// int main(void) {
//     printf("\n");
//     printf("╔════════════════════════════════════════════════════════╗\n");
//     printf("║  ohashtable v2 Comprehensive Test Suite               ║\n");
//     printf("║  Features: rm bit + Manual Expansion                  ║\n");
//     printf("╚════════════════════════════════════════════════════════╝\n");
//     printf("\n");
//
//     // Basic Functionality
//     printf(COLOR_YELLOW "=== Basic Functionality ===" COLOR_RESET "\n");
//     test_init_and_destroy();
//     test_basic_insert_get();
//     test_insert_replace();
//     test_take_ownership();
//     test_rm_bit_semantics();
//
//     // Tombstone and Probing
//     printf("\n" COLOR_YELLOW "=== Tombstone & Probing Chain ===" COLOR_RESET "\n");
//     test_tombstone_probing();
//
//     // Expiration
//     printf("\n" COLOR_YELLOW "=== Expiration ===" COLOR_RESET "\n");
//     test_expiration();
//     test_lazy_expiration_cleanup();
//
//     // Manual Expansion
//     printf("\n" COLOR_YELLOW "=== Manual Expansion ===" COLOR_RESET "\n");
//     test_manual_expansion();
//     test_expansion_with_tombstones();
//     test_expansion_with_expired();
//
//     // Edge Cases
//     printf("\n" COLOR_YELLOW "=== Edge Cases ===" COLOR_RESET "\n");
//     test_empty_key();
//     test_large_key();
//     test_collision_stress();
//     test_interleaved_operations();
//
//     // Performance
//     printf("\n" COLOR_YELLOW "=== Performance ===" COLOR_RESET "\n");
//     test_insert_performance();
//     test_get_performance();
//     test_mixed_workload_performance();
//     test_cache_friendliness();
//
//     // Memory Safety
//     printf("\n" COLOR_YELLOW "=== Memory Safety ===" COLOR_RESET "\n");
//     test_no_double_free();
//     test_expansion_memory_safety();
//
//     // Summary
//     printf("\n");
//     printf("╔════════════════════════════════════════════════════════╗\n");
//     printf("║  Test Summary                                          ║\n");
//     printf("╠════════════════════════════════════════════════════════╣\n");
//     printf("║  Total:  %3d                                           ║\n", g_test_count);
//     printf("║  " COLOR_GREEN "Passed: %3d" COLOR_RESET "                                           ║\n", g_test_passed);
//     printf("║  " COLOR_RED "Failed: %3d" COLOR_RESET "                                           ║\n", g_test_failed);
//     printf("╚════════════════════════════════════════════════════════╝\n");
//     printf("\n");
//
//     return g_test_failed == 0 ? 0 : 1;
// }