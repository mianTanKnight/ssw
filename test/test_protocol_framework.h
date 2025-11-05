//
// Created by wenshen on 2025/10/22.
//

//
// Protocol Test Framework - Extends common framework with RESP2-specific helpers
//
#ifndef TEST_PROTOCOL_FRAMEWORK_H
#define TEST_PROTOCOL_FRAMEWORK_H

#include "test_common_framework.h"
#include "../include/resp2parser.h"

// Helper function: setup test context
static inline void setup_test_context(struct connection_t *cn,
                                      struct parser_context *ctx,
                                      const char *data,
                                      size_t len) {
    memset(cn, 0, sizeof(*cn));
    cn->read_buffer = (char *) malloc(len + 1);
    assert(cn->read_buffer);
    memcpy(cn->read_buffer, data, len);
    cn->read_buffer[len] = '\0';
    cn->rb_size = len;
    cn->rb_offset = 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->connection = cn;
    ctx->state = COMPLETE;
}

// Helper function: cleanup test context
static inline void cleanup_test_context(struct connection_t *cn) {
    if (cn->read_buffer) {
        free(cn->read_buffer);
        cn->read_buffer = NULL;
    }
}

// Helper function: feed data incrementally (for fragmentation tests)
static inline void feed_data(struct connection_t *cn, const char *data, size_t len) {
    size_t new_size = cn->rb_size + len;
    char *new_buf = realloc(cn->read_buffer, new_size + 1);
    assert(new_buf);

    memcpy(new_buf + cn->rb_size, data, len);
    new_buf[new_size] = '\0';

    cn->read_buffer = new_buf;
    cn->rb_size = new_size;
}

void run_basic_tests(void);

void run_array_tests(void);

void run_edge_case_tests(void);

void run_fragmentation_tests(void);

void run_performance_tests(void);

void run_stress_tests(void);

#endif // TEST_PROTOCOL_FRAMEWORK_H
