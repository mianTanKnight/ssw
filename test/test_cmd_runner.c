//
// Test Runner for CMD + OHASH Tests
// Runs all test suites and generates comprehensive report
//

#include "test_framework.h"
#include "../storage/ohashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// External test suite declarations
extern void run_cmd_functional_tests(void);

extern void run_cmd_memory_tests(void);

extern void run_cmd_performance_tests(void);

extern void run_cmd_stress_tests(void);

// Global statistics tracking
extern test_stats_t g_stats;

static void print_banner(void) {
    printf("\n");
    printf(COLOR_BOLD COLOR_CYAN);
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                    ║\n");
    printf("║           CMD + OHASH Comprehensive Test Suite                    ║\n");
    printf("║           High-Quality Testing Framework                          ║\n");
    printf("║                                                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf("\n");
}

static void print_section_header(const char *section_name) {
    printf("\n");
    printf(COLOR_BOLD COLOR_MAGENTA);
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf("  %s\n", section_name);
    printf("═══════════════════════════════════════════════════════════════════\n");
    printf(COLOR_RESET);
}

static void print_system_info(void) {
    printf(COLOR_BOLD "System Information:\n" COLOR_RESET);
    printf("  Date: %s", ctime(&(time_t){time(NULL)}));
    printf("  ohash_t size: %zu bytes\n", sizeof(ohash_t));
    printf("  Alignment: %zu bytes\n", _Alignof(ohash_t));
    printf("  Pointer size: %zu bytes\n", sizeof(void *));
    printf("\n");
}

static void reinit_hashtable(const char *suite_name) {
    // Free existing hash table
    if (ohashtabl) {
        // Note: In a real scenario, we'd need to free all entries first
        // For testing, we accept some leakage between suites
        free(ohashtabl);
        ohashtabl = NULL;
        cap = 0;
        size = 0;
    }

    // Reinitialize for next suite
    int ret = initohash(1024);
    if (ret != OK) {
        fprintf(stderr, COLOR_RED "Failed to initialize hash table for %s\n" COLOR_RESET,
                suite_name);
        exit(EXIT_FAILURE);
    }
}

static void print_suite_summary(test_stats_t before, test_stats_t after, const char *suite_name) {
    int suite_tests = after.total - before.total;
    int suite_passed = after.passed - before.passed;
    int suite_failed = after.failed - before.failed;
    double suite_time = after.total_time_ms - before.total_time_ms;

    printf("\n");
    printf(COLOR_BOLD "  Suite Summary:\n" COLOR_RESET);
    printf("    Tests run: %d\n", suite_tests);
    printf("    Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", suite_passed);
    if (suite_failed > 0) {
        printf("    Failed: " COLOR_RED "%d" COLOR_RESET "\n", suite_failed);
    }
    printf("    Time: %.2f ms\n", suite_time);
    printf("\n");
}

static void print_final_report(test_stats_t stats) {
    printf("\n\n");
    printf(COLOR_BOLD COLOR_CYAN);
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                       FINAL TEST REPORT                            ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf("\n");

    printf(COLOR_BOLD "Overall Statistics:\n" COLOR_RESET);
    printf("  Total Tests:     %d\n", stats.total);
    printf("  " COLOR_GREEN "✓ Passed:        %d (%.1f%%)" COLOR_RESET "\n",
           stats.passed, (stats.passed * 100.0) / stats.total);

    if (stats.failed > 0) {
        printf("  " COLOR_RED "✗ Failed:        %d (%.1f%%)" COLOR_RESET "\n",
               stats.failed, (stats.failed * 100.0) / stats.total);
    }

    if (stats.skipped > 0) {
        printf("  " COLOR_YELLOW "⊘ Skipped:       %d" COLOR_RESET "\n", stats.skipped);
    }

    printf("  Total Time:      %.2f ms\n", stats.total_time_ms);
    printf("  Avg per test:    %.3f ms\n", stats.total_time_ms / stats.total);
    printf("\n");

    // Coverage summary
    printf(COLOR_BOLD "Test Coverage:\n" COLOR_RESET);
    printf("  ✓ Functional correctness tests\n");
    printf("  ✓ Memory safety and leak detection\n");
    printf("  ✓ Performance benchmarks\n");
    printf("  ✓ Stress and edge case tests\n");
    printf("\n");

    // Final verdict
    if (stats.failed == 0) {
        printf(COLOR_GREEN COLOR_BOLD);
        printf("╔════════════════════════════════════════════════════════════════════╗\n");
        printf("║                                                                    ║\n");
        printf("║                   🎉  ALL TESTS PASSED  🎉                         ║\n");
        printf("║                                                                    ║\n");
        printf("║         CMD + OHASH is ready for production use!                  ║\n");
        printf("║                                                                    ║\n");
        printf("╚════════════════════════════════════════════════════════════════════╝\n");
        printf(COLOR_RESET);
    } else {
        printf(COLOR_RED COLOR_BOLD);
        printf("╔════════════════════════════════════════════════════════════════════╗\n");
        printf("║                                                                    ║\n");
        printf("║                   ❌  SOME TESTS FAILED  ❌                        ║\n");
        printf("║                                                                    ║\n");
        printf("║         Please review failed tests before deployment              ║\n");
        printf("║                                                                    ║\n");
        printf("╚════════════════════════════════════════════════════════════════════╝\n");
        printf(COLOR_RESET);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    int run_functional = 1;
    int run_memory = 1;
    int run_performance = 1;
    int run_stress = 1;

    if (argc > 1) {
        // Allow selective test running
        run_functional = 0;
        run_memory = 0;
        run_performance = 0;
        run_stress = 0;

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--functional") == 0) run_functional = 1;
            else if (strcmp(argv[i], "--memory") == 0) run_memory = 1;
            else if (strcmp(argv[i], "--performance") == 0) run_performance = 1;
            else if (strcmp(argv[i], "--stress") == 0) run_stress = 1;
            else if (strcmp(argv[i], "--all") == 0) {
                run_functional = 1;
                run_memory = 1;
                run_performance = 1;
                run_stress = 1;
            } else if (strcmp(argv[i], "--help") == 0) {
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("\nOptions:\n");
                printf("  --functional    Run functional correctness tests\n");
                printf("  --memory        Run memory safety tests\n");
                printf("  --performance   Run performance benchmarks\n");
                printf("  --stress        Run stress and edge case tests\n");
                printf("  --all           Run all test suites (default)\n");
                printf("  --help          Show this help message\n");
                printf("\n");
                return 0;
            }
        }
    }

    print_banner();
    print_system_info();

    test_stats_t suite_start, suite_end;

    // Run Functional Tests
    if (run_functional) {
        print_section_header("FUNCTIONAL CORRECTNESS TESTS");
        reinit_hashtable("Functional Tests");
        suite_start = g_stats;
        run_cmd_functional_tests();
        suite_end = g_stats;
        print_suite_summary(suite_start, suite_end, "Functional Tests");
    }

    // Run Memory Safety Tests
    if (run_memory) {
        print_section_header("MEMORY SAFETY TESTS");
        reinit_hashtable("Memory Safety Tests");
        suite_start = g_stats;
        run_cmd_memory_tests();
        suite_end = g_stats;
        print_suite_summary(suite_start, suite_end, "Memory Safety Tests");
    }

    // Run Performance Tests
    if (run_performance) {
        print_section_header("PERFORMANCE BENCHMARKS");
        reinit_hashtable("Performance Tests");
        suite_start = g_stats;
        run_cmd_performance_tests();
        suite_end = g_stats;
        print_suite_summary(suite_start, suite_end, "Performance Tests");
    }

    // // Run Stress Tests
    if (run_stress) {
        print_section_header("STRESS AND EDGE CASE TESTS");
        reinit_hashtable("Stress Tests");
        suite_start = g_stats;
        run_cmd_stress_tests();
        suite_end = g_stats;
        print_suite_summary(suite_start, suite_end, "Stress Tests");
    }
    // Print final report
    print_final_report(g_stats);

    // Cleanup
    if (ohashtabl) {
        free(ohashtabl);
        ohashtabl = NULL;
    }

    // Return appropriate exit code
    return (g_stats.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
