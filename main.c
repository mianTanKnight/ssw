#include <stdio.h>
#include "protocol/resp2parser.h"
#include "server_/noblock_sserver.h"
#include "storage/xxhash.h"
#include "test/test_framework.h"
#define XXH_INLINE_ALL
#include "storage/xxhash.h"
#include "storage/ohashtable.h"
#include "storage/ohashtable.h"

int test_on_read(struct connection_t *ct) {
    int ret = 0;
    if (!ct->use_data && ((ret = bindctx(ct) < 0))) {
        syslog(LOG_ERR, "parser_ error code %s", strerror(-ret));
        return ret;
    }
    struct parser_context *ctx = ct->use_data;
    ret = zerocopy_proceed(ctx);
    printf("zerocopy proceed : %s \n", ctx->outframe.start_rbp);
    return ret;
}

int test_on_writer(struct connection_t *ct) {
    ct->write_buffer[0] = 'o';
    ct->write_buffer[1] = 'k';
    ct->write_buffer[2] = '\0';
    ct->wb_limit = 3;
    return 0;
}


// void test_() {
//     run_basic_tests();
//     run_array_tests();
//     run_edge_case_tests();
//     run_fragmentation_tests();
//     run_performance_tests();
//     run_stress_tests();
// }

// int main() {
//     test_();
// }

double get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

// 测试连续访问性能
#define SIZE 100000
#define ITERATIONS 1000

void test_aligned_64() {
    struct {
        char data[36];
    } __attribute__((aligned(64))) *table;
    table = aligned_alloc(64, sizeof(*table) * SIZE);

    double start = get_time_ns();
    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (int i = 0; i < SIZE - 10; i++) {
            // 模拟线性探测：连续访问10个节点
            volatile char sum = 0;
            for (int j = 0; j < 10; j++) {
                sum += table[i + j].data[0];
            }
        }
    }
    double elapsed = get_time_ns() - start;

    printf("64字节对齐: %.0f ms, 内存: %zu MB\n",
           elapsed / 1e6, (64 * SIZE) / (1024 * 1024));
    free(table);
}

void test_aligned_8() {
    struct {
        char data[32];
    } __attribute__((aligned(8))) *table;
    table = malloc(sizeof(*table) * SIZE);

    double start = get_time_ns();
    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (int i = 0; i < SIZE - 10; i++) {
            volatile char sum = 0;
            for (int j = 0; j < 10; j++) {
                sum += table[i + j].data[0];
            }
        }
    }
    double elapsed = get_time_ns() - start;

    printf("8字节对齐:  %.0f ms, 内存: %zu MB\n",
           elapsed / 1e6, (40 * SIZE) / (1024 * 1024));
    free(table);
}

static char *make_key_(const char *prefix, int num) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s_%d", prefix, num);
    return strdup(buf);
}

static int *make_value_(int val) {
    int *p = malloc(sizeof(int));
    *p = val;
    return p;
}

int main() {
}
