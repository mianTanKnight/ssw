#include <stdio.h>
#include "../include/resp2parser.h"
#include "../include/noblock_sserver.h"
#include "../include/xxhash.h"
#include "../test/test_protocol_framework.h"
#define XXH_INLINE_ALL
#include "resp2parser.h"
#include "cmd_.h"
#include "ohashtable.h"


inline int handle_request(struct parser_context *ctx) {
//     int ret = 0;
// #ifndef NDEBUG
//     if (!ctx) return -EINVAL;
// #endif
//     ret = zerocopy_proceed(ctx);
//     if (ret < 0) return ret;
//     if (ctx->state == WAITING) return 0;
//     if (ctx->outframe.type != ARRAYS) {
// #ifndef NDEBUG
//         syslog(LOG_WARNING, "handle_request error type of not array , give up");
// #endif
//         return 0;
//     }
//     const size_t alen = ctx->outframe.array_len;
//     if (!alen) return 0;
//     ret = zerocopy_proceed(ctx);
//     if (ret < 0) return ret;
//
//     const size_t head_len = ctx->outframe.data_len;
//     char *cmd_head = ctx->outframe.start_rbp;
//
//     switch (head_len) {
//         case 3:
//             // SET
//             if (cmd_head[0] == 'S' && cmd_head[1] == 'E' && cmd_head[2] == 'T') {
//                 ret = zerocopy_proceed(ctx);
//                 if (ctx->state == WAITING) return 0;
//
//                 if (ret < 0) return ret;
//
//
//                 break;
//             }
//             // GET
//             if (cmd_head[0] == 'G' && cmd_head[1] == 'E' && cmd_head[2] == 'T') {
//                 break;
//             }
//             // DEL
//             if (cmd_head[0] == 'D' && cmd_head[1] == 'E' && cmd_head[2] == 'L') {
//             }
//             break;
//         case 7:
//             // EXPIRED
//             if (cmd_head[0] == 'E' && cmd_head[1] == 'X' && cmd_head[2] == 'P' && cmd_head[3] == 'I' && cmd_head[4] ==
//                 'R' && cmd_head[5] == 'E' && cmd_head[6] == 'D') {
//             }
//             break;
//         default:
//             break; // not support
//     }
//     return 0;
}


int test_on_read(struct connection_t *ct) {
    int ret = 0;
    if (!ct->use_data && ((ret = bindctx(ct) < 0))) {
        syslog(LOG_ERR, "parser_ error code %s", strerror(-ret));
        return ret;
    }
    struct parser_context *ctx = ct->use_data;
    ret = handle_request(ctx);

    // printf("zerocopy proceed : %s \n", ctx->outframe.start_rbp);
    return ret;
}


int test_on_writer(struct connection_t *ct) {
    ct->write_buffer[0] = 'o';
    ct->write_buffer[1] = 'k';
    ct->write_buffer[2] = '\0';
    ct->wb_limit = 3;
    return 0;
}


// cmd_comparison_benchmark.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>  // for rdtsc

#define ITERATIONS 100000000

// ============================================================
// 方法1: memcmp (有循环)
// ============================================================
static inline int match_memcmp(const char *cmd, size_t len) {
    if (len == 3) {
        if (memcmp(cmd, "SET", 3) == 0) return 1;
        if (memcmp(cmd, "GET", 3) == 0) return 2;
        if (memcmp(cmd, "DEL", 3) == 0) return 3;
    }
    return 0;
}

// ============================================================
// 方法2: 手动字节比较 (展开的循环)
// ============================================================
static inline int match_manual_bytes(const char *cmd, size_t len) {
    if (len == 3) {
        if (cmd[0] == 'S' && cmd[1] == 'E' && cmd[2] == 'T') return 1;
        if (cmd[0] == 'G' && cmd[1] == 'E' && cmd[2] == 'T') return 2;
        if (cmd[0] == 'D' && cmd[1] == 'E' && cmd[2] == 'L') return 3;
    }
    return 0;
}

// ============================================================
// 方法3: 整数比较 - 直接指针转换 (可能未对齐)
// ============================================================
static inline int match_int_unsafe(const char *cmd, size_t len) {
    if (len == 3) {
        uint32_t cmd_int = *(uint32_t *) cmd & 0x00FFFFFF;
        switch (cmd_int) {
            case 0x544553: return 1; // "SET"
            case 0x544547: return 2; // "GET"
            case 0x4C4544: return 3; // "DEL"
        }
    }
    return 0;
}

// ============================================================
// 方法4: 整数比较 - 手动组合 (安全)
// ============================================================
static inline int match_int_safe(const char *cmd, size_t len) {
    if (len == 3) {
        uint32_t cmd_int = (uint32_t) cmd[0] |
                           ((uint32_t) cmd[1] << 8) |
                           ((uint32_t) cmd[2] << 16);
        switch (cmd_int) {
            case 0x544553: return 1; // "SET"
            case 0x544547: return 2; // "GET"
            case 0x4C4544: return 3; // "DEL"
        }
    }
    return 0;
}

// ============================================================
// 方法5: 第一字符分支 + 16位比较
// ============================================================
static inline int match_hybrid(const char *cmd, size_t len) {
    if (len == 3) {
        switch (cmd[0]) {
            case 'S':
                if (*(uint16_t *) (cmd + 1) == 0x5445) return 1; // "ET"
                break;
            case 'G':
                if (*(uint16_t *) (cmd + 1) == 0x5445) return 2; // "ET"
                break;
            case 'D':
                if (*(uint16_t *) (cmd + 1) == 0x4C45) return 3; // "EL"
                break;
        }
    }
    return 0;
}

// ============================================================
// 方法6: 完全展开的 if-else (编译器最容易优化)
// ============================================================
static inline int match_unrolled(const char *cmd, size_t len) {
    if (len != 3) return 0;

    // SET
    if (cmd[0] == 'S') {
        if (cmd[1] == 'E' && cmd[2] == 'T') return 1;
        return 0;
    }
    // GET
    if (cmd[0] == 'G') {
        if (cmd[1] == 'E' && cmd[2] == 'T') return 2;
        return 0;
    }
    // DEL
    if (cmd[0] == 'D') {
        if (cmd[1] == 'E' && cmd[2] == 'L') return 3;
        return 0;
    }
    return 0;
}

// ============================================================
// Benchmark 框架
// ============================================================
typedef int (*match_func)(const char *, size_t);

static inline uint64_t rdtsc_start() {
    unsigned cycles_low, cycles_high;
    __asm__ volatile (
        "CPUID\n\t"
        "RDTSC\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r" (cycles_high), "=r" (cycles_low)
        :: "%rax", "%rbx", "%rcx", "%rdx"
    );
    return ((uint64_t) cycles_high << 32) | cycles_low;
}

static inline uint64_t rdtsc_end() {
    unsigned cycles_low, cycles_high;
    __asm__ volatile (
        "RDTSCP\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "CPUID\n\t"
        : "=r" (cycles_high), "=r" (cycles_low)
        :: "%rax", "%rbx", "%rcx", "%rdx"
    );
    return ((uint64_t) cycles_high << 32) | cycles_low;
}

void benchmark(const char *name, match_func func, char *cmds[], size_t lens[], int count) {
    printf("\n[%s]\n", name);

    // 预热
    volatile int result;
    for (int i = 0; i < 1000; i++) {
        result = func(cmds[i % count], lens[i % count]);
    }

    // 使用 rdtsc 测量 CPU 周期
    uint64_t total_cycles = 0;
    uint64_t min_cycles = UINT64_MAX;

    for (int run = 0; run < 10; run++) {
        uint64_t start = rdtsc_start();

        for (int i = 0; i < ITERATIONS; i++) {
            result = func(cmds[i % count], lens[i % count]);
        }

        uint64_t end = rdtsc_end();
        uint64_t cycles = end - start;
        total_cycles += cycles;
        if (cycles < min_cycles) min_cycles = cycles;
    }

    (void) result; // 防止优化掉

    double avg_cycles = (double) total_cycles / (10.0 * ITERATIONS);
    double min_cycles_per_op = (double) min_cycles / ITERATIONS;

    printf("  Avg cycles/op: %.2f\n", avg_cycles);
    printf("  Min cycles/op: %.2f\n", min_cycles_per_op);
    printf("  Throughput:    %.2f M ops/sec (@ 3GHz)\n",
           3000.0 / avg_cycles);

    // 时间测量（作为参考）
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int i = 0; i < ITERATIONS; i++) {
        result = func(cmds[i % count], lens[i % count]);
    }

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("  Time/op:       %.2f ns\n", elapsed * 1e9 / ITERATIONS);
}

int main() {
    // 测试数据
    char *cmds[] = {"SET", "GET", "DEL", "SET", "GET"};
    size_t lens[] = {3, 3, 3, 3, 3};
    int count = 5;

    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║   Command String Comparison Benchmark               ║\n");
    printf("║   Iterations: %d                            ║\n", ITERATIONS);
    printf("╚══════════════════════════════════════════════════════╝\n");

    benchmark("1. memcmp (有循环)", match_memcmp, cmds, lens, count);
    benchmark("2. 手动字节比较", match_manual_bytes, cmds, lens, count);
    benchmark("3. 整数比较 - unsafe", match_int_unsafe, cmds, lens, count);
    benchmark("4. 整数比较 - safe", match_int_safe, cmds, lens, count);
    benchmark("5. 混合方案", match_hybrid, cmds, lens, count);
    benchmark("6. 完全展开", match_unrolled, cmds, lens, count);

    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("注意事项:\n");
    printf("  - 数值越小越好\n");
    printf("  - CPU 周期是最准确的性能指标\n");
    printf("  - 实际性能取决于 CPU、编译器优化等\n");
    printf("═══════════════════════════════════════════════════════\n");

    return 0;
}

// int main() {
// }
