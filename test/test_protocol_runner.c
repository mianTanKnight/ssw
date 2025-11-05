//
// Test Runner for RESP2 Protocol Parser Tests
// Runs all protocol test suites and generates comprehensive report
//

#include "test_protocol_framework.h"

// External test suite declarations
extern void run_basic_tests(void);
extern void run_array_tests(void);
extern void run_edge_case_tests(void);
extern void run_fragmentation_tests(void);
extern void run_performance_tests(void);

int main(int argc, char *argv[]) {
    // Parse command line arguments for selective test running
    int run_basic = 1;
    int run_arrays = 1;
    int run_edge = 1;
    int run_frag = 1;
    int run_perf = 1;

    if (argc > 1) {
        // Allow selective test running
        run_basic = 0;
        run_arrays = 0;
        run_edge = 0;
        run_frag = 0;
        run_perf = 0;

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--basic") == 0) run_basic = 1;
            else if (strcmp(argv[i], "--arrays") == 0) run_arrays = 1;
            else if (strcmp(argv[i], "--edge") == 0) run_edge = 1;
            else if (strcmp(argv[i], "--fragmentation") == 0) run_frag = 1;
            else if (strcmp(argv[i], "--performance") == 0) run_perf = 1;
            else if (strcmp(argv[i], "--all") == 0) {
                run_basic = 1;
                run_arrays = 1;
                run_edge = 1;
                run_frag = 1;
                run_perf = 1;
            } else if (strcmp(argv[i], "--help") == 0) {
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("\nOptions:\n");
                printf("  --basic          Run basic protocol tests\n");
                printf("  --arrays         Run array parsing tests\n");
                printf("  --edge           Run edge case tests\n");
                printf("  --fragmentation  Run packet fragmentation tests\n");
                printf("  --performance    Run performance benchmarks\n");
                printf("  --all            Run all test suites (default)\n");
                printf("  --help           Show this help message\n");
                printf("\n");
                return 0;
            }
        }
    }

    // Print banner
    printf("\n");
    printf(COLOR_BOLD COLOR_CYAN);
    printf("╔════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                    ║\n");
    printf("║           RESP2 Protocol Parser Comprehensive Test Suite          ║\n");
    printf("║           Zero-Copy Parsing & Fragmentation Handling              ║\n");
    printf("║                                                                    ║\n");
    printf("╚════════════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf("\n");

    // Run test suites
    if (run_basic) {
        run_basic_tests();
    }

    if (run_arrays) {
        run_array_tests();
    }

    if (run_edge) {
        run_edge_case_tests();
    }

    if (run_frag) {
        run_fragmentation_tests();
    }

    if (run_perf) {
        run_performance_tests();
    }

    // Print final report
    print_test_report();

    // Return appropriate exit code
    return (g_stats.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
