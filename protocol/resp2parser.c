//
// Created by wenshen on 2025/10/14.
//

#include "resp2parser.h"

int bindctx(struct connection_t *connection) {
#ifndef NDEBUG
    if (!connection) return -EINVAL;
#endif
    int ret = 0;
    if (!connection->use_data) ret = create_ctx(connection);
    return ret;
}
