//
// Created by wenshen on 2025/11/5.
//

#ifndef SSW_TEST_OHASH_FRAMEWORK_H
#define SSW_TEST_OHASH_FRAMEWORK_H

#include "test_common_framework.h"
#include "../include/ohashtable.h"

// Legacy compatibility macro
#define RUN_TEST(test_func) do { \
    TEST_START(#test_func); \
    setup(); \
    test_func(); \
    teardown(); \
    TEST_PASS(); \
} while (0)

// --- Helper Functions ---

// Setup and Teardown for each test
static inline void setup(void) {
    initohash(16); // Start with a small capacity
}

static inline void teardown(void) {
    // This is a simplified teardown. A real one might need a free_func.
    // We assume the test itself cleans up what it creates.
    free(ohashtabl);
    ohashtabl = NULL;
    cap = 0;
    size = 0;
}

// Proxy free function to test expand_capacity
static inline void free_key_value_pair(void *ptr) {
    // In our tests, keys and values are simple strings from strdup/malloc
    free(ptr);
}

// Data factories
static inline char *make_key(const char *base, int i) {
    char buf[64];
    sprintf(buf, "%s_%d", base, i);
    return strdup(buf);
}

static inline void *make_value(const char *base, int i) {
    char buf[64];
    sprintf(buf, "%s_val_%d", base, i);
    return strdup(buf);
}
#endif //SSW_TEST_OHASH_FRAMEWORK_H