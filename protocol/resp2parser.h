//
// Created by wenshen on 2025/10/14.
//

#ifndef SSW_RESP2PARSER_H
#define SSW_RESP2PARSER_H
#include "../server_/noblock_sserver.h"
#include <limits.h>

#if LLONG_MAX == 9223372036854775807LL
#define try_parser_num try_parser_positive_num_str_64
#else
#define try_parser_num try_parser_positive_num_str
#endif

/**
 * state 处理拆粘包的状态
 * 它不需要太复杂的状态
 * process之后
 * 数据不全 -> waiting
 * 数据足够 -> complete
 * complete 会移动 offset 但 waiting 不会
 * state会影响 buffer + offset 的解析策略
 *
 */
enum parse_state {
    WAITING,
    COMPLETE
};

enum protocol_type {
    /***  +<字符串内容>\r\n */
    SIMPLE_STR,
    /*** -<错误类型> <错误信息>\r\n */
    ERRORS,
    /*** :<带符号的64位整数>\r\n */
    NUMERIC,
    /***  $<字符串长度>\r\n<字符串数据>\r\n */
    BULK_STRINGS,
    /***  *<数组元素数量>\r\n<元素1><元素2>...<元素N> */
    ARRAYS
};

typedef enum parse_state parse_state;
typedef enum protocol_type protocol_type;

struct parser_out {
    protocol_type type;
    char *start_rbp;
    size_t data_len;
    size_t array_len;
};

struct parser_process {
    long long anchorpoint_offset; //帧锚点 指针
    char prefix;
    // $ 两阶段
    int have_bulk_len;
    long long bulk_len; // 解析后的 <len>
    size_t head_len; // "$<len>\\r\\n" 的长度
};

/**
 * @brief RESP2协议解析器的核心上下文。
 *
 * 1.  **零拷贝 (Zero-Copy)**:
 *     解析出的数据帧(outframe)中的指针直接指向原始读缓冲区(read_buffer)，
 *     避免任何内存拷贝，以实现极致性能。
 *
 * 2.  **流式分帧 (Streaming & Framing)**:
 *     解析器不关心业务逻辑或数据结构，其唯一职责是将TCP字节流精确地切分为
 *     一个个独立的RESP协议单元（帧）。数据结构的组织由上层调用者负责。
 *
 * 3.  **线性数组处理 (Linear Array Handling)**:
 *     为保证栈安全，数组按“帧”来线性解析。
 *     - 遇到 "*3\r\n"，解析器返回一个类型为 ARRAYS 的帧，告知上层“接下来有3个元素”。
 *     - 上层需要循环调用解析器3次，以依次获取每个元素帧。
 *     - 这种方式天然支持任意深度的数组嵌套。
 *
 * --- 调用模式 ---
 * for (;;) {
 *     ret = zerocopy_proceed(ctx);
 *     if (ctx->state == COMPLETE && ret == 0) {
 *         // 解析成功一个帧，处理 ctx->outframe
 *     } else {
 *         // 数据不足(WAITING)或出错，跳出循环等待更多数据
 *         break;
 *     }
 * }
 */
struct parser_context {
    parse_state state; // COMPLETE | WAITING
    struct connection_t *connection; // 双向绑定
    // 输出与进度
    struct parser_out outframe; // 当前帧数据
    struct parser_process prog; // 解析进度
};

/*********************** inline of hot path ******************************/
/**
 *  accₖ = Σ_{i=0}^{k−1} dᵢ · 10^{k−1−i}
 *
 */
static inline long long
try_parser_positive_num_str_64(const char *restrict bf, size_t len) {
#ifndef NDEBUG
    if (!bf) return -EINVAL;
#endif
    if (!len) return 0;
    long long acc = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char uc = (unsigned char) bf[i];
        if (uc < '0' || uc > '9') return -1;
        if (i >= 18) {
            if (acc > (LLONG_MAX - (uc - '0')) / 10) return -1;
        }
        acc = acc * 10 + (uc - '0');
    }
    return acc;
}

static inline long long
try_parser_positive_num_str(const char *restrict bf, size_t len) {
#ifndef NDEBUG
    if (!bf) return -EINVAL;
#endif
    if (!len) return 0;
    long long acc = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char uc = (unsigned char) bf[i];
        if (uc < '0' || uc > '9') return -1;
        acc = acc * 10 + (uc - '0');
    }
    return acc;
}

static const uint8_t is_prefix[256] = {
    ['+'] = 1, // is_prefix[43] = 1  ('+' 的 ASCII 是 43)
    ['-'] = 1, // is_prefix[45] = 1  ('-' 的 ASCII 是 45)
    [':'] = 1, // is_prefix[58] = 1
    ['$'] = 1, // is_prefix[36] = 1
    ['*'] = 1 // is_prefix[42] = 1
};

static const uint8_t is_prefix_simple[256] = {
    ['+'] = 1, // is_prefix[43] = 1
    ['-'] = 1, // is_prefix[45] = 1
    [':'] = 1, // is_prefix[58] = 1
};

static const protocol_type is_prefix_type[256] = {
    ['+'] = SIMPLE_STR,
    ['-'] = ERRORS,
    [':'] = NUMERIC,
    ['$'] = BULK_STRINGS,
    ['*'] = ARRAYS
};

static inline protocol_type get_protocol_type_array(const char prefix) {
    return is_prefix_type[(unsigned char) prefix];
}

static inline protocol_type get_protocol_type(const char prefix) {
    switch (prefix) {
        case '+':
            return SIMPLE_STR;
        case '-':
            return ERRORS;
        case ':':
            return NUMERIC;
        case '$':
            return BULK_STRINGS;
            break;
        default:
            return ARRAYS;
    }
}

/**
 * get_next_crlf_r_step2_basis_inline 是一个内联方法 存在于hot path
 * 它不会检查 buffer 是否 null_p
 * 此函数性质为2步伐快速检查 也就是说 cap的单双检查并不会在循环内
 * find next complete CRLF
 */
static inline long long get_next_crlf_step2_basis_inline(const char *buffer, long long cap) {
    if (cap < 2) return 0; // if < 2 no complete// to evenNumber
    long long cap_ = cap;
    if (cap & 1ULL)
        cap_ = cap - 1;
    for (long long i = 0; i < cap_ - 1; i += 2) {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n')
            return i;
        if (buffer[i + 1] == '\r' && buffer[i + 2] == '\n')
            return i + 1;
    }
    if (cap - cap_) {
        // 如果是单数 (cap一定比cap_大1，如果cap是单数)
        // 那么检查最后一个单数对应的字符是不是'\n' 如果是继续检查 prev是不是 '\r'
        if (buffer[cap - 1] == '\n' && buffer[cap - 2] == '\r') return cap - 2;
    }
    return -1;
}


/**
 * find next complete CRLF
 */
static inline long long get_next_crlf_memchr_inline(const char *buffer, unsigned long long cap) {
    if (cap < 2) return -1;
    const char *p = buffer;
    const char *end = buffer + cap - 1;
    while ((p = memchr(p, '\r', end - p))) {
        if (p[1] == '\n')
            return p - buffer;
        p++;
    }
    return -1;
}

/**
 * find next complete CRLF's \r
 * return maybe null;
 */
static inline char *get_next_crlf_rp_memchr_inline(const char *buffer, unsigned long long cap) {
    return memchr(buffer, '\r', cap - 1);
}


static inline void clear_prog(struct parser_context *ctx) {
    ctx->prog.anchorpoint_offset = 0;
    ctx->prog.bulk_len = 0;
    ctx->prog.have_bulk_len = 0;
    ctx->prog.prefix = 0;
    ctx->prog.head_len = 0;
}

/**
 * @brief       在读缓冲区(read_buffer)上向前推进RESP2协议解析。
 * @details     本函数是解析器的核心引擎。它会从上次停止的位置(rb_offset)开始
 *              扫描缓冲区，尝试解析出一个完整的RESP协议帧。
 *
 *              - 如果成功解析一个帧:
 *                  - 结果将填充到 ctx->outframe。
 *                  - 状态将置为 COMPLETE。
 *                  - 会更新 connection->rb_offset 以消耗已解析的字节。
 *                  - 返回 0。
 *
 *              - 如果缓冲区数据不足以构成一个完整帧:
 *                  - 内部解析进度会保存在 ctx->prog 中。
 *                  - 状态将置为 WAITING。
 *                  - connection->rb_offset **不会**改变。
 *                  - 返回 0。
 *
 * @param[in,out] ctx  解析器上下文，包含状态、输入缓冲区和输出结果。
 * @return      0: 成功 (无论是解析完毕还是需要等待)。
 * @return      <0: 发生协议错误或内部错误 (如 -EPROTO, -EMSGSIZE)。
 *              发生错误时，rb_offset 仍然会更新以跳过错误的数据段。
 */
static inline int zerocopy_proceed(struct parser_context *ctx) {
#ifndef NDEBUG
    if (!ctx) return -EINVAL;
    if (!ctx->connection) {
        syslog(LOG_ERR, "proceed fatal error : connection is null");
        return -EPIPE;
    }
    if (ctx->connection->rb_offset > ctx->connection->rb_size) {
        syslog(LOG_ERR, "[%d]:proceed fatal error : offset > size", ctx->connection->fd);
        return -EPIPE;
    }
#endif

    int ret = 0;
    parse_state state = ctx->state;
    struct connection_t *cn = ctx->connection;

    long long bufferlen = cn->rb_size,
            bufferoffset = cn->rb_offset,
            remaining = bufferlen - bufferoffset,
            consumed = 0;
    char *start = cn->read_buffer + bufferoffset;

    if (state == COMPLETE) {
        char prefix = 0;
        char *anchorpoint = NULL;
        if (cn->rb_offset == cn->rb_size)
            goto waitingout;

        long long i;
        for (i = 0; i < remaining; ++i) {
            char c = start[i];
            if (is_prefix[(unsigned char) c]) {
                prefix = c;
                anchorpoint = start + i;
                break;
            }
        }
        if (!prefix) {
            ctx->prog.prefix = prefix;
            goto waitingout;
        }
        long long next_crlf_len = get_next_crlf_memchr_inline(anchorpoint + 1, remaining);
        if (next_crlf_len < 0) {
            ctx->prog.prefix = prefix;
            goto waitingout;
        }

        i++; // I start of 0 but count is 1
        remaining -= i;
        long long next_crlf_len_end = next_crlf_len + 2;
        remaining -= next_crlf_len_end;
        if (is_prefix_simple[(unsigned char) prefix]) {
            ctx->outframe.start_rbp = anchorpoint + 1;
            ctx->outframe.data_len = next_crlf_len;
            ctx->outframe.type = get_protocol_type_array(prefix);
            consumed = i + next_crlf_len_end;
            goto compleout;
        }
        // linear analytical non-iterative control
        if (prefix == '$' && !ctx->prog.have_bulk_len) {
            long long dlen = try_parser_num(anchorpoint + 1, next_crlf_len);
            if (dlen < 0) {
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol",ctx->connection->fd);
                ret = -EPROTO;
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            if (dlen >= BUFFER_SIZE_MAX) {
                syslog(LOG_WARNING, "[%d]proceed error : invalid number to long of $ protocol", ctx->connection->fd);
                ret = -EMSGSIZE; // EMSGSIZE 需要外部特殊处理
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            ctx->prog.have_bulk_len = 1;
            ctx->prog.bulk_len = dlen;
            ctx->prog.head_len = next_crlf_len_end + i;
            // try skip data
            // 1: 保证 data/r/n 协议的完整(剩余的并且要足够并以\r\n结尾)
            long long skipN = dlen + 2;
            if (skipN > remaining) {
                // no enough, + 2 is CRLF
                ctx->prog.anchorpoint_offset = i - 1;
                ctx->prog.prefix = prefix;
                goto waitingout;
            }
            consumed = i + next_crlf_len_end + skipN;
            // enough
            const long long need = 1 + next_crlf_len_end + dlen;
            if (*(anchorpoint + need) == '\r' && *(anchorpoint + need + 1) == '\n') {
                ctx->outframe.start_rbp = anchorpoint + ctx->prog.head_len;
                ctx->outframe.data_len = ctx->prog.bulk_len;
                ctx->outframe.type = BULK_STRINGS;
                goto compleout;
            }
            syslog(LOG_WARNING, "[%d]proceed error : /*CRLF*/ error of $ protocol", ctx->connection->fd);
            ret = -EPROTO;
            goto errorpprotocol;
        }
        // Simple but not crude
        if (prefix == '*') {
            long long arraylen = try_parser_num(anchorpoint + 1, next_crlf_len);
            if (arraylen < 0) {
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol",ctx->connection->fd);
                ret = -EPROTO;
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            if (arraylen >= ARRAY_SIZE_MAX) {
                syslog(LOG_WARNING, "[%d]proceed error : invalid number to long of $ protocol",ctx->connection->fd);
                ret = -EMSGSIZE; // EMSGSIZE 需要外部特殊处理
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            ctx->outframe.type = ARRAYS;
            ctx->outframe.array_len = arraylen;
            ctx->outframe.start_rbp = anchorpoint + 1;
            ctx->outframe.data_len = next_crlf_len;
            consumed = i + next_crlf_len_end;
        }
    } else {
        // ctx->prog.anchorpoint_offset 是起点 它只可能是 crlf 中的 /r 或 prefix
        // 如果是 $ 并且解析出了 head_len 就是有效偏移量
        char prefix_waiting = ctx->prog.prefix;
        char *anchorpoint_start = start + ctx->prog.anchorpoint_offset;

        long long i = 0;
        if (!prefix_waiting) {
            for (i = 0; i < remaining; ++i) {
                char c = anchorpoint_start[i];
                if (is_prefix[(unsigned char) c]) {
                    prefix_waiting = c;
                    anchorpoint_start = anchorpoint_start + i;
                    break;
                }
            }
            if (!prefix_waiting) goto waitingout;
        }

        long long next_crlf_len = get_next_crlf_memchr_inline(anchorpoint_start + 1, remaining);
        if (next_crlf_len < 0) {
            ctx->prog.prefix = prefix_waiting;
            goto waitingout;
        }

        i++;
        remaining -= i;
        long long next_crlf_len_end = next_crlf_len + 2;
        remaining -= next_crlf_len_end;

        if (is_prefix_simple[(unsigned char) prefix_waiting]) {
            ctx->outframe.start_rbp = anchorpoint_start + 1;
            ctx->outframe.data_len = next_crlf_len;
            ctx->outframe.type = get_protocol_type_array(prefix_waiting);
            consumed = i + next_crlf_len_end;
            goto compleout;
        }

        if (prefix_waiting == '$') {
            if (!ctx->prog.have_bulk_len) {
                long long dlen = try_parser_num(anchorpoint_start + 1, next_crlf_len);
                if (dlen < 0) {
                    syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol",ctx->connection->fd);
                    ret = -EPROTO;
                    consumed = i + next_crlf_len_end;
                    goto errorpprotocol;
                }
                if (dlen >= BUFFER_SIZE_MAX) {
                    syslog(LOG_WARNING, "[%d]:proceed error : invalid number to long of $ protocol",ctx->connection->fd);
                    ret = -EMSGSIZE; // EMSGSIZE 需要外部特殊处理
                    consumed = i + next_crlf_len_end;
                    goto errorpprotocol;
                }
                ctx->prog.have_bulk_len = 1;
                ctx->prog.bulk_len = dlen;
                ctx->prog.head_len = next_crlf_len_end + i;
            }
            long long dlen = ctx->prog.bulk_len;

            // try skip data
            // 1: 保证 data/r/n 协议的完整(剩余的并且要足够并以\r\n结尾)
            long long skipN = dlen + 2;
            if (skipN > remaining) {
                // no enough, + 2 is CRLF
                ctx->prog.anchorpoint_offset += i - 1;
                ctx->prog.prefix = prefix_waiting;
                goto waitingout;
            }
            consumed = i + next_crlf_len_end + skipN;
            // enough
            long long need = 1 + next_crlf_len_end + dlen;
            if (*(anchorpoint_start + need) == '\r' && *(anchorpoint_start + need + 1) == '\n') {
                ctx->outframe.start_rbp = anchorpoint_start + ctx->prog.head_len;
                ctx->outframe.data_len = ctx->prog.bulk_len;
                ctx->outframe.type = BULK_STRINGS;
                goto compleout;
            }
            syslog(LOG_WARNING, "[%d]:proceed error : /*CRLF*/ error of $ protocol", ctx->connection->fd);
            ret = -EPROTO;
            goto errorpprotocol;
        }

        if (prefix_waiting == '*') {
            long long arraylen = try_parser_num(anchorpoint_start + 1, next_crlf_len);
            if (arraylen < 0) {
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol",ctx->connection->fd);
                ret = -EPROTO;
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            if (arraylen >= ARRAY_SIZE_MAX) {
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number to long of $ protocol",ctx->connection->fd);
                ret = -EMSGSIZE; // EMSGSIZE 需要外部特殊处理
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            ctx->outframe.type = ARRAYS;
            ctx->outframe.array_len = arraylen;
            ctx->outframe.start_rbp = anchorpoint_start + 1;
            ctx->outframe.data_len = next_crlf_len;
            consumed = i + next_crlf_len_end;
        }
    }
compleout:
    ctx->state = COMPLETE;
    memset(&ctx->prog, 0, sizeof(ctx->prog));
    ctx->connection->rb_offset += consumed;
    return 0;
waitingout:
    ctx->state = WAITING;
    return 0;
errorpprotocol:
    ctx->state = COMPLETE;
    memset(&ctx->prog, 0, sizeof(ctx->prog));
    ctx->connection->rb_offset += consumed;
    return ret;
}


static inline int create_ctx(struct connection_t *connection) {
    struct parser_context *ctx_n = calloc(sizeof(struct parser_context), 1);
    if (!ctx_n) return -ENOMEM;
    ctx_n->connection = connection;
    ctx_n->state = COMPLETE; // start is complete
    connection->use_data = (void *) ctx_n;
    return 0;
}

int bindctx(struct connection_t *connection);

#endif //SSW_RESP2PARSER_H
