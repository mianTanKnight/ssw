#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_ohash_framework.h"

// External test suite declarations
extern void test_init_and_destroy(void);

extern void test_basic_insert_get(void);

extern void test_insert_replace(void);

extern void test_take_ownership(void);

extern void test_tombstone_probing(void);

extern void test_expiration(void);

extern void test_expansion_cleans_tombstones(void);

extern void test_manual_expansion(void);


int main() {
    printf("\n"
        "╔════════════════════════════════════════════════════════╗\n"
        "║  ohashtable Comprehensive Test Suite                  ║\n"
        "╚════════════════════════════════════════════════════════╝\n\n");

    printf("=== Basic Functionality ===\n");
    RUN_TEST(test_init_and_destroy);
    RUN_TEST(test_basic_insert_get);
    RUN_TEST(test_insert_replace);
    RUN_TEST(test_take_ownership);

    printf("\n=== Tombstone & Probing Chain ===\n");
    RUN_TEST(test_tombstone_probing);

    printf("\n=== Expiration ===\n");
    RUN_TEST(test_expiration);

    printf("\n=== Expansion ===\n");
    RUN_TEST(test_manual_expansion);
    RUN_TEST(test_expansion_cleans_tombstones);


    // Print test report from common framework
    print_test_report();

    // Return appropriate exit code
    return (g_stats.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
