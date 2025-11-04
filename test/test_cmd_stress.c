//
// Stress and Edge Case Tests for CMD + OHASH
// Tests: Boundary conditions, extreme inputs, robustness under pressure
//

#include "test_framework.h"
#include "../command/cmd_.h"
#include <string.h>
#include <assert.h>
#include <limits.h>

// Test 1: Maximum key length boundary
static void test_max_key_length_boundary(void) {
    TEST_START("Maximum key length boundary");

    // MAX_KEY_LEN is (1U << 30) - 1 = 1073741823
    // We can't allocate that much, so test validation logic
    uint32_t max_valid = MAX_KEY_LEN;
    uint32_t invalid = max_valid + 1;

    // Test with practical large key
    uint32_t practical_large = 1024 * 1024; // 1MB
    char *large_key = malloc(practical_large);
    ASSERT_NOT_NULL(large_key, "Large key allocation should succeed");
    memset(large_key, 'K', practical_large);

    const char *value = "value";
    int ret = SET4dup(large_key, practical_large, value, strlen(value), 0);
    ASSERT_TRUE(ret >= 0 || ret == FULL, "Large key should be accepted");

    if (ret >= 0) {
        osv *result = GET(large_key, practical_large);
        ASSERT_NOT_NULL(result, "Large key should be retrievable");
        DEL(large_key, practical_large, free);
    }

    free(large_key);

#ifndef NDEBUG
    // In debug mode, invalid length should trigger assert
    // Skip this part in debug builds
#else
    // In release mode, test that invalid length is rejected gracefully
    char small_key[] = "key";
    ret = SET4dup(small_key, invalid, value, strlen(value), 0);
    ASSERT_LT(ret, 0, "Invalid key length should be rejected");
#endif

    TEST_PASS();
}

// Test 2: Maximum value size
static void test_max_value_size(void) {
    TEST_START("Maximum value size");

    const char *key = "max_value_key";
    uint32_t keylen = strlen(key);

    // Test 10MB value (practical limit)
    size_t large_size = 10 * 1024 * 1024;
    char *large_value = malloc(large_size);
    ASSERT_NOT_NULL(large_value, "Large value allocation should succeed");
    memset(large_value, 'V', large_size);

    int ret = SET4dup(key, keylen, large_value, large_size, 0);
    ASSERT_TRUE(ret >= 0 || ret == FULL, "Large value should be stored");

    if (ret >= 0) {
        osv *result = GET((char *) key, keylen);
        ASSERT_NOT_NULL(result, "Large value should be retrievable");
        ASSERT_EQ(result->vlen, large_size, "Value size should match");

        DEL((char *) key, keylen, free);
    }

    free(large_value);
    TEST_PASS();
}

// Test 3: Extreme number of operations
static void test_extreme_operations(void) {
    TEST_START("Extreme number of operations");

    const int num_ops = 500000;
    char key[64], value[128];
    int successful_ops = 0;

    for (int i = 0; i < num_ops; i++) {
        snprintf(key, sizeof(key), "stress_key_%d", i % 50000);
        snprintf(value, sizeof(value), "stress_value_%d", i);

        int op = i % 3;
        if (op == 0) {
            // SET
            int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
            if (ret == FULL) {
                ret = SET4dup(key, strlen(key), value, strlen(value), 0);
            }
            if (ret >= 0) successful_ops++;
        } else if (op == 1) {
            // GET
            osv *result = GET(key, strlen(key));
            if (result) successful_ops++;
        } else {
            // DEL
            DEL(key, strlen(key), free);
            successful_ops++;
        }
    }

    printf("\n      Total operations: %d\n", num_ops);
    printf("      Successful: %d\n", successful_ops);
    printf("      Success rate: %.2f%%\n", (successful_ops * 100.0) / num_ops);

    ASSERT_GT(successful_ops, num_ops * 0.5, "Success rate should be > 50%");

    TEST_PASS();
}

// Test 4: Key with special characters
static void test_special_character_keys(void) {
    TEST_START("Keys with special characters");

    const char *special_keys[] = {
        "\x01\x02\x03", // Control characters
        "\xFF\xFE\xFD", // High bytes
        "key\0with\0nulls", // Embedded nulls (only first part used)
        "key\nwith\nnewlines", // Newlines
        "key\twith\ttabs", // Tabs
        "key with spaces", // Spaces
        "key\"with\"quotes", // Quotes
        "key'with'quotes", // Single quotes
        "key\\with\\backslash", // Backslashes
        "key/with/slashes", // Slashes
    };

    const int num_keys = sizeof(special_keys) / sizeof(special_keys[0]);
    const char *value = "special_value";

    for (int i = 0; i < num_keys; i++) {
        uint32_t keylen = strlen(special_keys[i]);
        int ret = SET4dup(special_keys[i], keylen, value, strlen(value), 0);
        ASSERT_TRUE(ret >= 0 || ret == FULL, "Special character key should be accepted");

        osv *result = GET((char *) special_keys[i], keylen);
        if (ret >= 0) {
            ASSERT_NOT_NULL(result, "Special character key should be retrievable");
        }
    }

    // Cleanup
    for (int i = 0; i < num_keys; i++) {
        DEL((char *) special_keys[i], strlen(special_keys[i]), free);
    }

    TEST_PASS();
}

// Test 5: Rapid SET/DEL cycles
static void test_rapid_set_del_cycles(void) {
    TEST_START("Rapid SET/DEL cycles");

    const char *key = "cycle_key";
    const char *value = "cycle_value";
    uint32_t keylen = strlen(key);
    const int num_cycles = 10000;

    for (int i = 0; i < num_cycles; i++) {
        int ret = SET4dup(key, keylen, value, strlen(value), 0);
        ASSERT_TRUE(ret >= 0 || ret == FULL, "SET should succeed in cycle");

        osv *result = GET((char *) key, keylen);
        ASSERT_NOT_NULL(result, "GET should find key in cycle");

        ret = DEL((char *) key, keylen, free);
        ASSERT_EQ(ret, 0, "DEL should succeed in cycle");

        result = GET((char *) key, keylen);
        ASSERT_NULL(result, "Key should not exist after DEL in cycle");
    }

    printf("\n      Cycles completed: %d\n", num_cycles);

    TEST_PASS();
}

// Test 6: Expiration edge cases
static void test_expiration_edge_cases(void) {
    TEST_START("Expiration edge cases");

    char key[64];
    const char *value = "value";
    time_t now = get_current_time_seconds();

    // Test 1: Expiration at exact current time
    snprintf(key, sizeof(key), "expire_now");
    int ret = SET4dup(key, strlen(key), value, strlen(value), (uint32_t) now);
    ASSERT_TRUE(ret >= 0 || ret == FULL, "SET with current time should succeed");

    osv *result = GET(key, strlen(key));
    ASSERT_NULL(result, "Key expiring at current time should be expired");

    // Test 2: Expiration 1 second in future
    snprintf(key, sizeof(key), "expire_soon");
    ret = SET4dup(key, strlen(key), value, strlen(value), (uint32_t) (now + 1));
    ASSERT_TRUE(ret >= 0 || ret == FULL, "SET with future time should succeed");

    result = GET(key, strlen(key));
    ASSERT_NOT_NULL(result, "Key with future expiration should exist");

    // Test 3: Maximum expiration time (UINT32_MAX)
    snprintf(key, sizeof(key), "expire_far_future");
    ret = SET4dup(key, strlen(key), value, strlen(value), UINT32_MAX);
    ASSERT_TRUE(ret >= 0 || ret == FULL, "SET with max time should succeed");

    result = GET(key, strlen(key));
    ASSERT_NOT_NULL(result, "Key with far future expiration should exist");

    // Test 4: Zero expiration (no expiration)
    snprintf(key, sizeof(key), "expire_never");
    ret = SET4dup(key, strlen(key), value, strlen(value), 0);
    ASSERT_TRUE(ret >= 0 || ret == FULL, "SET with zero expiration should succeed");

    result = GET(key, strlen(key));
    ASSERT_NOT_NULL(result, "Key with no expiration should exist");

    TEST_PASS();
}

// Test 7: Hash table full scenario
static void test_hash_table_full(void) {
    TEST_START("Hash table near-full scenario");

    uint64_t initial_cap = cap;
    char key[64], value[128];

    // Fill to near capacity (90% of load factor threshold)
    size_t target_fills = (size_t) (initial_cap * 0.7 * 0.9);
    size_t current_size = size;
    size_t to_insert = target_fills > current_size ? target_fills - current_size : 0;

    for (size_t i = 0; i < to_insert; i++) {
        snprintf(key, sizeof(key), "full_test_%zu", i);
        snprintf(value, sizeof(value), "value_%zu", i);
        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        ASSERT_TRUE(ret >= 0, "SET should succeed even near full");
    }

    double load_factor = (double) size / cap;
    printf("\n      Final load factor: %.3f\n", load_factor);
    printf("      Size: %" PRIu64 ", Capacity: %" PRIu64 "\n", size, cap);
    TEST_PASS();
}

// Test 8: Same key different lengths (prefix matching)
static void test_key_prefix_matching(void) {
    TEST_START("Key prefix matching");

    const char *keys[] = {
        "key",
        "key1",
        "key12",
        "key123",
        "key1234",
    };
    const int num_keys = sizeof(keys) / sizeof(keys[0]);

    // Insert all keys with different values
    for (int i = 0; i < num_keys; i++) {
        char value[64];
        snprintf(value, sizeof(value), "value_%d", i);
        int ret = SET4dup(keys[i], strlen(keys[i]), value, strlen(value), 0);
        ASSERT_TRUE(ret >= 0 || ret == FULL, "SET should succeed");
    }

    // Verify each key retrieves correct value
    for (int i = 0; i < num_keys; i++) {
        osv *result = GET((char *) keys[i], strlen(keys[i]));
        ASSERT_NOT_NULL(result, "Each key should be independently retrievable");

        char expected[64];
        snprintf(expected, sizeof(expected), "value_%d", i);
        ASSERT_STR_EQ(result->d, expected, strlen(expected), "Value should match");
    }

    // Cleanup
    for (int i = 0; i < num_keys; i++) {
        DEL((char *) keys[i], strlen(keys[i]), free);
    }

    TEST_PASS();
}

// Test 9: Concurrent-like access pattern (simulated)
static void test_interleaved_operations(void) {
    TEST_START("Interleaved operations pattern");

    const int num_keys = 1000;
    char keys[1000][64];
    char values[1000][128];

    // Phase 1: Insert odd indices
    for (int i = 1; i < num_keys; i += 2) {
        snprintf(keys[i], 64, "interleaved_%d", i);
        snprintf(values[i], 128, "value_%d", i);
        int ret = SET4dup(keys[i], strlen(keys[i]), values[i], strlen(values[i]), 0);
        ASSERT_TRUE(ret >= 0 || ret == FULL, "Odd SET should succeed");
    }

    // Phase 2: Insert even indices
    for (int i = 0; i < num_keys; i += 2) {
        snprintf(keys[i], 64, "interleaved_%d", i);
        snprintf(values[i], 128, "value_%d", i);
        int ret = SET4dup(keys[i], strlen(keys[i]), values[i], strlen(values[i]), 0);
        ASSERT_TRUE(ret >= 0 || ret == FULL, "Even SET should succeed");
    }

    // Phase 3: Delete odd indices
    for (int i = 1; i < num_keys; i += 2) {
        DEL(keys[i], strlen(keys[i]), free);
    }

    // Phase 4: Verify even indices still exist
    for (int i = 0; i < num_keys; i += 2) {
        osv *result = GET(keys[i], strlen(keys[i]));
        ASSERT_NOT_NULL(result, "Even keys should still exist");
    }

    // Phase 5: Re-insert odd indices
    for (int i = 1; i < num_keys; i += 2) {
        int ret = SET4dup(keys[i], strlen(keys[i]), values[i], strlen(values[i]), 0);
        ASSERT_TRUE(ret >= 0 || ret == REMOVED, "Odd re-insert should succeed");
    }

    // Phase 6: Verify all keys exist
    for (int i = 0; i < num_keys; i++) {
        osv *result = GET(keys[i], strlen(keys[i]));
        ASSERT_NOT_NULL(result, "All keys should exist after re-insert");
    }

    TEST_PASS();
}

// Test 10: Memory pressure test
static void test_memory_pressure(void) {
    TEST_START("Memory pressure test");

    const int num_large_entries = 100;
    const size_t entry_size = 1024 * 1024; // 1MB each
    char key[64];

    printf("\n      Inserting %d x %zu byte entries\n", num_large_entries, entry_size);

    for (int i = 0; i < num_large_entries; i++) {
        snprintf(key, sizeof(key), "memory_pressure_%d", i);

        char *large_value = malloc(entry_size);
        if (!large_value) {
            printf("      Memory allocation failed at entry %d\n", i);
            break;
        }
        memset(large_value, (char) i, entry_size);

        int ret = SET4dup(key, strlen(key), large_value, entry_size, 0);
        free(large_value);

        if (ret == FULL) {
            ret = SET4dup(key, strlen(key), large_value, entry_size, 0);
        }

        if (ret < 0) {
            printf("      SET failed at entry %d\n", i);
            break;
        }
    }

    // Verify and cleanup
    int verified = 0;
    for (int i = 0; i < num_large_entries; i++) {
        snprintf(key, sizeof(key), "memory_pressure_%d", i);
        osv *result = GET(key, strlen(key));
        if (result) {
            verified++;
            ASSERT_EQ(result->vlen, entry_size, "Entry size should match");
        }
        DEL(key, strlen(key), free);
    }

    printf("      Verified %d entries\n", verified);
    ASSERT_GT(verified, num_large_entries / 2, "At least half should be stored");

    TEST_PASS();
}

// Test 11: Pathological linear probing
static void test_pathological_probing(void) {
    TEST_START("Pathological linear probing");

    // Create keys that hash to same bucket
    const int num_collisions = 100;
    char key[64], value[64];

    // Use sequential keys that will likely collide
    for (int i = 0; i < num_collisions; i++) {
        snprintf(key, sizeof(key), "probe_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);

        int ret = SET4dup(key, strlen(key), value, strlen(value), 0);
        ASSERT_TRUE(ret >= 0 || ret == FULL, "SET should handle probing");
    }

    // Verify all are retrievable
    for (int i = 0; i < num_collisions; i++) {
        snprintf(key, sizeof(key), "probe_%d", i);
        osv *result = GET(key, strlen(key));
        ASSERT_NOT_NULL(result, "All colliding keys should be retrievable");
    }

    TEST_PASS();
}

// Test 12: Boundary value testing
static void test_boundary_values(void) {
    TEST_START("Boundary value testing");

    struct {
        const char *key;
        uint32_t keylen;
        const char *value;
        uint64_t vallen;
    } test_cases[] = {
                {"", 0, "value", 5}, // Empty key
                {"k", 1, "", 0}, // Single char key, empty value
                {"k", 1, "v", 1}, // Minimal key and value
                {"key", 3, "value", 5}, // Normal case
            };

    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        int ret = SET4dup(test_cases[i].key, test_cases[i].keylen,
                          test_cases[i].value, test_cases[i].vallen, 0);

        if (test_cases[i].keylen == 0) {
            // Empty key might be rejected
            continue;
        }

        ASSERT_TRUE(ret >= 0 || ret == FULL, "Boundary case should be handled");

        if (ret >= 0) {
            osv *result = GET((char *) test_cases[i].key, test_cases[i].keylen);
            if (result) {
                ASSERT_EQ(result->vlen, test_cases[i].vallen, "Value length should match");
            }
        }
    }

    TEST_PASS();
}

// Test runner
void run_cmd_stress_tests(void) {
    TEST_SUITE_START("CMD + OHASH Stress and Edge Case Tests");

    // Initialize hash table
    int ret = initohash(1024);
    assert(ret == OK && "Hash table initialization failed");

    test_max_key_length_boundary();
    test_max_value_size();
    test_extreme_operations();
    test_special_character_keys();
    test_rapid_set_del_cycles();
    test_expiration_edge_cases();
    test_hash_table_full();
    test_key_prefix_matching();
    test_interleaved_operations();
    test_memory_pressure();
    test_pathological_probing();
    test_boundary_values();

    TEST_SUITE_END();
}
