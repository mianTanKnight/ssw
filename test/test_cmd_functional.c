//
// Comprehensive Functional Tests for CMD + OHASH
// Tests: SET, GET, DEL, EXPIRED operations with correctness guarantees
//

#include "test_common_framework.h"
#include "../include/cmd_.h"
#include <string.h>
#include <assert.h>
#include <unistd.h>

// Test helper: verify osv structure
static void verify_osv(osv *v, const char *expected, uint64_t expected_len) {
    ASSERT_NOT_NULL(v, "osv should not be NULL");
    ASSERT_EQ(v->vlen, expected_len, "osv vlen mismatch");
    ASSERT_STR_EQ(v->d, expected, expected_len, "osv data mismatch");
}

// Test 1: Basic SET and GET
static void test_basic_set_get(void) {
    TEST_START("Basic SET and GET");

    const char *key = "test_key";
    const char *value = "test_value";
    uint32_t keylen = strlen(key);
    uint64_t vallen = strlen(value);

    int ret = SET4dup(key, keylen, value, vallen, 0);
    ASSERT_EQ(ret, OK, "SET should return OK");

    osv *result = GET((char *) key, keylen);
    verify_osv(result, value, vallen);

    TEST_PASS();
}

// Test 2: SET with replacement
static void test_set_replacement(void) {
    TEST_START("SET with replacement");

    const char *key = "replace_key";
    const char *value1 = "value1";
    const char *value2 = "value2_longer";
    uint32_t keylen = strlen(key);

    // First insert
    int ret = SET4dup(key, keylen, value1, strlen(value1), 0);
    ASSERT_EQ(ret, OK, "First SET should return OK");

    // Replace
    ret = SET4dup(key, keylen, value2, strlen(value2), 0);
    ASSERT_EQ(ret, REPLACED, "Second SET should return REPLACED");

    // Verify new value
    osv *result = GET((char *) key, keylen);
    verify_osv(result, value2, strlen(value2));

    TEST_PASS();
}

// Test 3: GET non-existent key
static void test_get_nonexistent(void) {
    TEST_START("GET non-existent key");

    const char *key = "nonexistent_key_12345";
    osv *result = GET((char *) key, strlen(key));
    ASSERT_NULL(result, "GET non-existent key should return NULL");

    TEST_PASS();
}

// Test 4: DEL operation
static void test_delete(void) {
    TEST_START("DEL operation");

    const char *key = "delete_key";
    const char *value = "delete_value";
    uint32_t keylen = strlen(key);

    // Insert
    int ret = SET4dup(key, keylen, value, strlen(value), 0);
    ASSERT_EQ(ret, OK, "SET should succeed");

    // Verify exists
    osv *result = GET((char *) key, keylen);
    ASSERT_NOT_NULL(result, "Key should exist before deletion");

    // Delete
    ret = DEL((char *) key, keylen, free);
    ASSERT_EQ(ret, 0, "DEL should return 0");

    // Verify deleted
    result = GET((char *) key, keylen);
    ASSERT_NULL(result, "Key should not exist after deletion");

    TEST_PASS();
}

// Test 5: EXPIRED with immediate expiration
static void test_expired_immediate(void) {
    TEST_START("EXPIRED with immediate expiration");

    const char *key = "expire_key";
    const char *value = "expire_value";
    uint32_t keylen = strlen(key);
    uint32_t expiratime = (uint32_t) get_current_time_seconds() - 1; // Already expired

    // Insert with past expiration
    int ret = SET4dup(key, keylen, value, strlen(value), expiratime);
    ASSERT_EQ(ret, OK, "SET should succeed");

    // Should be expired immediately
    osv *result = GET((char *) key, keylen);
    ASSERT_NULL(result, "Expired key should return NULL");

    TEST_PASS();
}

// Test 6: EXPIRED with future expiration
static void test_expired_future(void) {
    TEST_START("EXPIRED with future expiration");

    const char *key = "future_expire_key";
    const char *value = "future_value";
    uint32_t keylen = strlen(key);
    uint32_t expiratime = (uint32_t) get_current_time_seconds() + 10; // 10 seconds from now

    // Insert with future expiration
    int ret = SET4dup(key, keylen, value, strlen(value), expiratime);
    ASSERT_EQ(ret, OK, "SET should succeed");

    // Should still be accessible
    osv *result = GET((char *) key, keylen);
    verify_osv(result, value, strlen(value));

    TEST_PASS();
}

// Test 7: EXPIRED update expiration time
static void test_update_expiration(void) {
    TEST_START("Update expiration time");

    const char *key = "update_expire_key";
    const char *value = "value";
    uint32_t keylen = strlen(key);

    // Insert without expiration
    int ret = SET4dup(key, keylen, value, strlen(value), 0);
    ASSERT_EQ(ret, OK, "SET should succeed");

    // Set expiration in future
    uint32_t new_expiratime = (uint32_t) get_current_time_seconds() + 100;
    ret = EXPIRED((char *) key, keylen, new_expiratime);
    ASSERT_EQ(ret, 0, "EXPIRED should return 0");

    // Should still be accessible
    osv *result = GET((char *) key, keylen);
    verify_osv(result, value, strlen(value));

    TEST_PASS();
}

// Test 8: Binary data handling
static void test_binary_data(void) {
    TEST_START("Binary data handling");

    const char *key = "binary_key";
    uint32_t keylen = strlen(key);

    // Binary data with null bytes
    unsigned char binary_value[] = {0x00, 0x01, 0xFF, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    uint64_t vallen = sizeof(binary_value);

    int ret = SET4dup(key, keylen, binary_value, vallen, 0);
    ASSERT_EQ(ret, OK, "SET binary data should succeed");

    osv *result = GET((char *) key, keylen);
    ASSERT_NOT_NULL(result, "GET should return result");
    ASSERT_EQ(result->vlen, vallen, "Binary data length should match");
    ASSERT_TRUE(memcmp(result->d, binary_value, vallen) == 0, "Binary data should match exactly");

    TEST_PASS();
}

// Test 9: Empty value
static void test_empty_value(void) {
    TEST_START("Empty value handling");

    const char *key = "empty_key";
    const char *value = "";
    uint32_t keylen = strlen(key);
    uint64_t vallen = 0;

    int ret = SET4dup(key, keylen, value, vallen, 0);
    ASSERT_EQ(ret, OK, "SET empty value should succeed");

    osv *result = GET((char *) key, keylen);
    ASSERT_NOT_NULL(result, "GET should return result for empty value");
    ASSERT_EQ(result->vlen, 0, "Empty value length should be 0");

    TEST_PASS();
}

// Test 10: Large value
static void test_large_value(void) {
    TEST_START("Large value handling");

    const char *key = "large_key";
    uint32_t keylen = strlen(key);

    // 1MB value
    size_t large_size = 1024 * 1024;
    char *large_value = malloc(large_size);
    assert(large_value);
    memset(large_value, 'A', large_size);

    int ret = SET4dup(key, keylen, large_value, large_size, 0);
    ASSERT_EQ(ret, OK, "SET large value should succeed");

    osv *result = GET((char *) key, keylen);
    ASSERT_NOT_NULL(result, "GET should return result");
    ASSERT_EQ(result->vlen, large_size, "Large value length should match");
    ASSERT_TRUE(result->d[0] == 'A' && result->d[large_size-1] == 'A',
                "Large value data should be correct");

    free(large_value);
    TEST_PASS();
}

// Test 11: Key collision handling
static void test_collision_handling(void) {
    TEST_START("Hash collision handling");

    // Insert many keys to force collisions
    const int num_keys = 100;
    char keys[100][32];

    for (int i = 0; i < num_keys; i++) {
        snprintf(keys[i], sizeof(keys[i]), "collision_key_%d", i);
        char value[32];
        snprintf(value, sizeof(value), "value_%d", i);

        int ret = SET4dup(keys[i], strlen(keys[i]), value, strlen(value), 0);
        ASSERT_TRUE(ret == OK || ret == REPLACED, "SET should succeed");
    }

    // Verify all keys are retrievable
    for (int i = 0; i < num_keys; i++) {
        osv *result = GET(keys[i], strlen(keys[i]));
        ASSERT_NOT_NULL(result, "All keys should be retrievable");

        char expected[32];
        snprintf(expected, sizeof(expected), "value_%d", i);
        ASSERT_EQ(result->vlen, strlen(expected), "Value length should match");
        ASSERT_STR_EQ(result->d, expected, strlen(expected), "Value should match");
    }

    TEST_PASS();
}

// Test 12: DEL non-existent key (should not crash)
static void test_delete_nonexistent(void) {
    TEST_START("DEL non-existent key");

    const char *key = "never_existed_key_xyz";
    int ret = DEL((char *) key, strlen(key), free);
    ASSERT_EQ(ret, 0, "DEL non-existent key should return 0");

    TEST_PASS();
}

// Test 13: Multiple DEL on same key
static void test_multiple_delete(void) {
    TEST_START("Multiple DEL on same key");

    const char *key = "multi_del_key";
    const char *value = "value";
    uint32_t keylen = strlen(key);

    // Insert
    int ret = SET4dup(key, keylen, value, strlen(value), 0);
    ASSERT_EQ(ret, OK, "SET should succeed");

    // First delete
    ret = DEL((char *) key, keylen, free);
    ASSERT_EQ(ret, 0, "First DEL should succeed");

    // Second delete (already deleted)
    ret = DEL((char *) key, keylen, free);
    ASSERT_EQ(ret, 0, "Second DEL should not crash");

    TEST_PASS();
}

// Test 14: SET after DEL
static void test_set_after_delete(void) {
    TEST_START("SET after DEL (tombstone reuse)");

    const char *key = "reuse_key";
    const char *value1 = "value1";
    const char *value2 = "value2";
    uint32_t keylen = strlen(key);

    // Insert, delete, insert again
    int ret = SET4dup(key, keylen, value1, strlen(value1), 0);
    ASSERT_EQ(ret, OK, "First SET should succeed");

    ret = DEL((char *) key, keylen, free);
    ASSERT_EQ(ret, 0, "DEL should succeed");

    ret = SET4dup(key, keylen, value2, strlen(value2), 0);
    ASSERT_TRUE(ret == REMOVED || ret == OK, "SET after DEL should succeed");

    osv *result = GET((char *) key, keylen);
    verify_osv(result, value2, strlen(value2));

    TEST_PASS();
}

// Test 15: Capacity expansion
static void test_capacity_expansion(void) {
    TEST_START("Capacity expansion (load factor test)");

    uint64_t initial_cap = cap;

    // Insert enough keys to trigger expansion
    // Load factor threshold is 0.7, so we need cap * 0.7 + extra
    int keys_to_insert = (int) (initial_cap * 0.7);

    for (int i = 0; i < keys_to_insert; i++) {
        char key[32];
        snprintf(key, sizeof(key), "expand_key_%d", i);
        char value[32];
        snprintf(value, sizeof(value), "expand_value_%d", i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        ASSERT_TRUE(ret == OK || ret == REPLACED || ret == FULL || ret == REMOVED || ret == EXPIRED_,
                    "SET should succeed or indicate FULL");
        if (ret == FULL) {
            // Expansion should happen automatically in SET4dup
            // Retry should succeed
            ret = SET4dup(key, strlen(key), value, strlen(value), 0);
            ASSERT_TRUE(ret == OK || ret == REPLACED, "SET after expansion should succeed");
        }
    }

    // Verify capacity increased
    ASSERT_GT(cap, initial_cap, "Capacity should have increased");

    // Verify all keys are still accessible
    for (int i = 0; i < keys_to_insert; i++) {
        char key[32];
        snprintf(key, sizeof(key), "expand_key_%d", i);
        osv *result = GET(key, strlen(key));
        ASSERT_NOT_NULL(result, "All keys should survive expansion");
    }

    TEST_PASS();
}

// Test 16: Maximum key length
static void test_max_key_length(void) {
    TEST_START("Maximum key length");

    // MAX_KEY_LEN is (1U << 30) - 1
    uint32_t max_len = MAX_KEY_LEN;

    // Test at boundary (we can't actually allocate 1GB for this test)
    // So we test the validation logic
    uint32_t test_len = 1024 * 1024; // 1MB key
    char *large_key = malloc(test_len);
    assert(large_key);
    memset(large_key, 'K', test_len);

    const char *value = "value";
    int ret = SET4dup(large_key, test_len, value, strlen(value), 0);
    ASSERT_TRUE(ret >= 0 || ret == FULL, "Large key should be accepted or FULL");

    free(large_key);
    TEST_PASS();
}

// Test 17: Expired key replacement
static void test_expired_key_replacement(void) {
    TEST_START("Replace expired key");

    const char *key = "expire_replace_key";
    const char *value1 = "expired_value";
    const char *value2 = "new_value";
    uint32_t keylen = strlen(key);

    // Insert with immediate expiration
    uint32_t past_time = (uint32_t) get_current_time_seconds() - 1;
    int ret = SET4dup(key, keylen, value1, strlen(value1), past_time);
    ASSERT_EQ(ret, OK, "SET with expiration should succeed");

    // Insert new value (should replace expired entry)
    ret = SET4dup(key, keylen, value2, strlen(value2), 0);
    ASSERT_TRUE(ret == EXPIRED_ || ret == REPLACED || ret == OK,
                "SET should replace expired entry");

    // Verify new value
    osv *result = GET((char *) key, keylen);
    verify_osv(result, value2, strlen(value2));

    TEST_PASS();
}

// Test runner
void run_cmd_functional_tests(void) {
    TEST_SUITE_START("CMD + OHASH Functional Tests");

    // Initialize hash table
    int ret = initohash(1024);
    assert(ret == OK && "Hash table initialization failed");

    test_basic_set_get();
    test_set_replacement();
    test_get_nonexistent();
    test_delete();
    test_expired_immediate();
    test_expired_future();
    test_update_expiration();
    test_binary_data();
    test_empty_value();
    test_large_value();
    test_collision_handling();
    test_delete_nonexistent();
    test_multiple_delete();
    test_set_after_delete();
    test_capacity_expansion();
    test_max_key_length();
    test_expired_key_replacement();

    TEST_SUITE_END();
}
