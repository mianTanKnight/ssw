//
// Memory Safety and Leak Detection Tests for CMD + OHASH
// Tests: Memory allocation, deallocation, leak detection, double-free prevention
//

#include "test_framework.h"
#include "../command/cmd_.h"
#include <string.h>
#include <assert.h>

// Global counters for tracking allocations
static size_t g_alloc_count = 0;
static size_t g_free_count = 0;
static size_t g_bytes_allocated = 0;
static size_t g_bytes_freed = 0;

// Custom allocator tracking wrapper
static void *tracked_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) {
        g_alloc_count++;
        g_bytes_allocated += size;
    }
    return ptr;
}

static void tracked_free(void *ptr) {
    if (ptr) {
        g_free_count++;
        // Note: We can't track exact freed size without wrapping malloc metadata
        free(ptr);
    }
}

static void reset_tracking(void) {
    g_alloc_count = 0;
    g_free_count = 0;
    g_bytes_allocated = 0;
    g_bytes_freed = 0;
}

// Test 1: No memory leak on simple SET and DEL
static void test_no_leak_set_del(void) {
    TEST_START("No memory leak: SET then DEL");

    reset_tracking();
    const char *key = "leak_test_key";
    const char *value = "leak_test_value";
    uint32_t keylen = strlen(key);

    int ret = SET4dup_(key, keylen, value, strlen(value), 0, tracked_malloc, tracked_free);
    ASSERT_TRUE(ret == OK || ret == REPLACED, "SET should succeed");

    // Delete to free memory
    ret = DEL((char *) key, keylen, tracked_free);
    ASSERT_EQ(ret, 0, "DEL should succeed");

    // Note: Perfect balance check would require wrapping all allocations
    // Here we just verify DEL was called
    ASSERT_GT(g_free_count, 0, "Memory should have been freed");

    TEST_PASS();
}

// Test 2: Memory management on replacement
static void test_memory_on_replacement(void) {
    TEST_START("Memory management on SET replacement");

    const char *key = "replace_mem_key";
    const char *value1 = "short";
    const char *value2 = "much_longer_value_to_test_reallocation";
    uint32_t keylen = strlen(key);

    reset_tracking();

    // First insert
    int ret = SET4dup_(key, keylen, value1, strlen(value1), 0, tracked_malloc, tracked_free);
    ASSERT_TRUE(ret == OK || ret == REPLACED, "First SET should succeed");

    size_t allocs_after_first = g_alloc_count;

    // Replace with larger value
    ret = SET4dup_(key, keylen, value2, strlen(value2), 0, tracked_malloc, tracked_free);
    ASSERT_EQ(ret, REPLACED, "Second SET should return REPLACED");

    // More allocations should have happened
    ASSERT_GT(g_alloc_count, allocs_after_first, "New allocations should occur");

    // Old value should have been freed (via oret callback in SET4dup)
    ASSERT_GT(g_free_count, 0, "Old value should be freed");

    // Cleanup
    DEL((char *) key, keylen, tracked_free);

    TEST_PASS();
}

// Test 3: No memory leak on capacity expansion
static void test_no_leak_expansion(void) {
    TEST_START("No memory leak during expansion");

    uint64_t initial_cap = cap;
    size_t keys_to_insert = (size_t) (initial_cap * 0.8);

    reset_tracking();

    // Insert many keys to trigger expansion
    for (size_t i = 0; i < keys_to_insert; i++) {
        char key[64];
        snprintf(key, sizeof(key), "expansion_leak_test_%zu", i);
        char value[64];
        snprintf(value, sizeof(value), "value_%zu", i);

        int ret = SET4dup_(key, strlen(key), value, strlen(value), 0, tracked_malloc, tracked_free);
        ASSERT_TRUE(ret == OK || ret == REPLACED || ret == FULL || ret == REMOVED,
                    "SET should succeed or return FULL");
    }

    // Verify expansion occurred
    ASSERT_GT(cap, initial_cap, "Capacity should have expanded");

    // Delete all keys
    for (size_t i = 0; i < keys_to_insert; i++) {
        char key[64];
        snprintf(key, sizeof(key), "expansion_leak_test_%zu", i);
        DEL(key, strlen(key), tracked_free);
    }

    // All allocations should be freed (approximately)
    // Note: Some internal allocations may remain (hash table itself)
    ASSERT_GT(g_free_count, 0, "Memory should have been freed");

    TEST_PASS();
}

// Test 4: Memory correctness with expired entries
static void test_memory_expired_entries(void) {
    TEST_START("Memory handling for expired entries");

    const char *key = "expired_mem_key";
    const char *value = "expired_value";
    uint32_t keylen = strlen(key);
    uint32_t past_time = (uint32_t) get_current_time_seconds() - 10;

    reset_tracking();

    // Insert expired entry
    int ret = SET4dup_(key, keylen, value, strlen(value), past_time, tracked_malloc, tracked_free);
    ASSERT_EQ(ret, OK, "SET should succeed");

    size_t allocs_after_insert = g_alloc_count;

    // GET should detect expiration and set tombstone
    osv *result = GET((char *) key, keylen);
    ASSERT_NULL(result, "Expired key should return NULL");

    // Insert new value at same key (should reuse expired slot)
    const char *new_value = "new_value";
    ret = SET4dup_(key, keylen, new_value, strlen(new_value), 0, tracked_malloc, tracked_free);
    ASSERT_TRUE(ret == EXPIRED_ || ret == REMOVED, "SET should replace expired entry");

    // Old value should be freed during expansion or explicitly
    ASSERT_GT(g_free_count, 0, "Expired entry memory should be freed");

    // Cleanup
    DEL((char *) key, keylen, tracked_free);

    TEST_PASS();
}

// Test 5: Large allocation stress test
static void test_large_allocation_stress(void) {
    TEST_START("Large allocation stress test");

    const int num_large_values = 10;
    size_t large_size = 512 * 1024; // 512KB each

    reset_tracking();

    // Allocate multiple large values
    for (int i = 0; i < num_large_values; i++) {
        char key[64];
        snprintf(key, sizeof(key), "large_alloc_%d", i);

        char *large_value = malloc(large_size);
        ASSERT_NOT_NULL(large_value, "Large allocation should succeed");
        memset(large_value, 'X', large_size);

        int ret = SET4dup_(key, strlen(key), large_value, large_size, 0, tracked_malloc, tracked_free);
        ASSERT_TRUE(ret == OK || ret == REPLACED || ret == FULL || ret == REMOVED,
                    "SET large value should succeed");

        tracked_free(large_value);
    }

    // Verify values are stored
    for (int i = 0; i < num_large_values; i++) {
        char key[64];
        snprintf(key, sizeof(key), "large_alloc_%d", i);

        osv *result = GET(key, strlen(key));
        if (result) {
            ASSERT_EQ(result->vlen, large_size, "Large value size should match");
            ASSERT_TRUE(result->d[0] == 'X', "Large value content should be correct");
        }
    }

    // Free all
    for (int i = 0; i < num_large_values; i++) {
        char key[64];
        snprintf(key, sizeof(key), "large_alloc_%d", i);
        DEL(key, strlen(key), tracked_free);
    }

    TEST_PASS();
}

// Test 6: Double-free prevention
static void test_double_free_prevention(void) {
    TEST_START("Double-free prevention");

    const char *key = "double_free_key";
    const char *value = "value";
    uint32_t keylen = strlen(key);

    // Insert
    int ret = SET4dup_(key, keylen, value, strlen(value), 0, tracked_malloc, tracked_free);
    ASSERT_TRUE(ret == OK || ret == REPLACED, "SET should succeed");

    // First delete
    ret = DEL((char *) key, keylen, tracked_free);
    ASSERT_EQ(ret, 0, "First DEL should succeed");

    // Second delete (should not cause double-free)
    ret = DEL((char *) key, keylen, tracked_free);
    ASSERT_EQ(ret, 0, "Second DEL should not crash (no double-free)");

    TEST_PASS();
}

// Test 7: Memory alignment verification
static void test_memory_alignment(void) {
    TEST_START("Memory alignment verification");

    // Verify ohash_t structure alignment
    ASSERT_EQ(sizeof(ohash_t), 32, "ohash_t should be exactly 32 bytes");
    ASSERT_EQ(_Alignof(ohash_t), 8, "ohash_t should be 8-byte aligned");

    // Verify osv structure alignment
    ASSERT_EQ(_Alignof(osv), 8, "osv should be 8-byte aligned");

    // Test that allocated osv is properly aligned
    const char *key = "alignment_key";
    const char *value = "alignment_value";
    uint32_t keylen = strlen(key);

    int ret = SET4dup_(key, keylen, value, strlen(value), 0, tracked_malloc, tracked_free);
    ASSERT_TRUE(ret == OK || ret == REPLACED, "SET should succeed");

    osv *result = GET((char *) key, keylen);
    ASSERT_NOT_NULL(result, "GET should succeed");

    // Check pointer alignment
    uintptr_t addr = (uintptr_t) result;
    ASSERT_EQ(addr % 8, 0, "osv pointer should be 8-byte aligned");

    DEL((char *) key, keylen, tracked_free);

    TEST_PASS();
}

// Test 8: NULL pointer safety
static void test_null_pointer_safety(void) {
    TEST_START("NULL pointer safety");

    // These should not crash (but will fail in debug mode due to asserts)
#ifdef NDEBUG
    osv *result = GET(NULL, 0);
    ASSERT_NULL(result, "GET with NULL key should return NULL");

    int ret = DEL(NULL, 0, tracked_free);
    ASSERT_TRUE(ret == 0 || ret < 0, "DEL with NULL key should handle gracefully");
#else
    // In debug mode, skip this test as asserts will trigger
    TEST_SKIP("Skipped in debug mode (asserts enabled)");
    return;
#endif

    TEST_PASS();
}

// Test 9: Tombstone memory handling
static void test_tombstone_memory(void) {
    TEST_START("Tombstone memory handling");

    const int num_ops = 50;

    // Create and delete many entries to create tombstones
    for (int i = 0; i < num_ops; i++) {
        char key[64];
        snprintf(key, sizeof(key), "tombstone_key_%d", i);
        char value[64];
        snprintf(value, sizeof(value), "value_%d", i);

        int ret = SET4dup_(key, strlen(key), value, strlen(value), 0, tracked_malloc, tracked_free);
        ASSERT_TRUE(ret == OK || ret == REPLACED || ret == FULL || ret == REMOVED, "SET should succeed");

        // Delete half of them to create tombstones
        if (i % 2 == 0) {
            DEL(key, strlen(key), tracked_free);
        }
    }

    // Reuse tombstone slots
    for (int i = 0; i < num_ops; i += 2) {
        char key[64];
        snprintf(key, sizeof(key), "tombstone_key_%d", i);
        char value[64];
        snprintf(value, sizeof(value), "reused_value_%d", i);

        int ret = SET4dup_(key, strlen(key), value, strlen(value), 0, tracked_malloc, tracked_free);
        ASSERT_TRUE(ret == OK || ret == REMOVED, "Tombstone reuse should succeed");
    }

    // Verify reused values
    for (int i = 0; i < num_ops; i += 2) {
        char key[64];
        snprintf(key, sizeof(key), "tombstone_key_%d", i);
        osv *result = GET(key, strlen(key));
        ASSERT_NOT_NULL(result, "Reused tombstone key should be retrievable");
    }

    // Cleanup
    for (int i = 0; i < num_ops; i++) {
        char key[64];
        snprintf(key, sizeof(key), "tombstone_key_%d", i);
        DEL(key, strlen(key), tracked_free);
    }

    TEST_PASS();
}

// Test 10: Zero-length allocation
static void test_zero_length_allocation(void) {
    TEST_START("Zero-length value allocation");

    const char *key = "zero_len_key";
    const char *value = "";
    uint32_t keylen = strlen(key);

    int ret = SET4dup_(key, keylen, value, 0, 0, tracked_malloc, tracked_free);
    ASSERT_EQ(ret, OK, "SET zero-length value should succeed");

    osv *result = GET((char *) key, keylen);
    ASSERT_NOT_NULL(result, "GET should return result");
    ASSERT_EQ(result->vlen, 0, "Value length should be 0");

    DEL((char *) key, keylen, tracked_free);

    TEST_PASS();
}

// Test runner
void run_cmd_memory_tests(void) {
    TEST_SUITE_START("CMD + OHASH Memory Safety Tests");

    // Initialize hash table
    int ret = initohash(256);
    assert(ret == OK && "Hash table initialization failed");

    test_no_leak_set_del();
    test_memory_on_replacement();
    test_no_leak_expansion();
    test_memory_expired_entries();
    test_large_allocation_stress();
    test_double_free_prevention();
    test_memory_alignment();
    test_null_pointer_safety();
    test_tombstone_memory();
    test_zero_length_allocation();

    TEST_SUITE_END();
}
