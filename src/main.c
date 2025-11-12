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
    int ret = 0;
#ifndef NDEBUG
    if (!ctx) return -EINVAL;
#endif
    ret = zerocopy_proceed(ctx);
    if (ret < 0) return ret;
    if (ctx->state == WAITING) return 0;

    ret = segment_proceed(ctx);
    if (ret < 0) return ret;

    if (ctx->segment_context.consumed) {
        size_t element_count_ = ctx->segment_context.element_count;
        if (element_count_ > 0) {
            struct element *elements = ctx->segment_context.elements;
            const char *cmd_head = elements[0].data;
            const size_t head_len = elements[0].len;
            switch (head_len) {
                case 3:
                    // GET
                    if (cmd_head[0] == 'S' && cmd_head[1] == 'E' && cmd_head[2] == 'T') {
                        uint32_t expired = 0;
                        if (element_count_ == 4) {
                            long long pn = try_parser_num(elements[3].data, elements[3].len);
                            if (pn < 0) {
                                ret = -EPROTO;
                                break;
                            }
                            expired = pn;
                        }
                        ret = SET4dup(elements[1].data, elements[1].len, elements[2].data, elements[2].len, expired);
                        break;
                    }
                    // SET
                    if (cmd_head[0] == 'G' && cmd_head[1] == 'E' && cmd_head[2] == 'T') {
                        GET(elements[1].data, elements[1].len);
                        break;
                    }
                    // DEL
                    if (cmd_head[0] == 'D' && cmd_head[1] == 'E' && cmd_head[2] == 'L') {
                        break;
                    }
                    ret = -EPROTO;
                    break;
                case 7:
                    // EXPIRED
                    if (cmd_head[0] == 'E' && cmd_head[1] == 'X' && cmd_head[2] == 'P' && cmd_head[3] == 'I' && cmd_head
                        [4] == 'R' && cmd_head[5] == 'E' && cmd_head[6] == 'D') {
                        break;
                    }
                    ret = -EPROTO;
                    break;
                default:
                    ret = -EPROTO;
                    break; // not support
            }
        }
    }

    return ret;
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


// int main() {
// }
