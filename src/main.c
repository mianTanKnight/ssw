#include <stdio.h>
#include "../include/resp2parser.h"
#include "../include/noblock_sserver.h"
#include "../include/xxhash.h"
#include "../test/test_protocol_framework.h"
#define XXH_INLINE_ALL
#include "xxhash.h"
#include "ohashtable.h"

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

int main() {
}
