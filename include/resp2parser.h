//
// Created by wenshen on 2025/10/14.
//

#ifndef SSW_RESP2PARSER_H
#define SSW_RESP2PARSER_H
#include "noblock_sserver.h"
#include "limits.h"
#include "errno.h"

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
 * @brief RESP2协议解析器的分帧层上下文 (Framing Layer Context)。
 * @details
 *      这是两阶段解析流水线的第一阶段。它的核心角色是一个高性能、无状态的
 *      “分帧器”或“词法分析器”(Tokenizer)。
 *
 *      其唯一职责，就是将无结构的TCP字节流，高效、精确地切割成一个个独立的、
 *      符合RESP2协议语法的原子单元（“帧”），并作为 `outframe` 输出。
 *
 * --- 核心设计原则 ---
 *
 * 1.  **零拷贝 (Zero-Copy)**:
 *     为实现极致性能，解析出的 `outframe` 中的指针直接指向原始读缓冲区
 *     (`connection->read_buffer`)，杜绝任何内存拷贝。整个流水线中的数据流转
 *     本质上是指针的传递。
 *
 * 2.  **流式分帧 (Streaming & Framing)**:
 *     本上下文不关心命令的逻辑结构（如一个数组命令需要几个元素）。它以“不中断”
 *     的模式工作，只要缓冲区有数据，就会持续地产出帧。这种设计将协议的物理层
 *     解析与逻辑层聚合完全解耦。
 *
 * 3.  **线性数组处理 (Linear Array Handling)**:
 *     为保证栈安全并简化设计，数组被当作“指令”来处理。
 *     - 遇到 "*3\r\n"，解析器仅返回一个类型为 ARRAYS 的“数组头帧”，告知
 *       流水线的下一阶段：“接下来有3个元素”。
 *     - 它将状态化跟踪数组元素的复杂性，完全委托给下一阶段（如 segment_context）。
 *
 * --- 驱动模式 ---
 *      `zerocopy_proceed` 由I/O事件驱动，在一个循环中被连续调用，以耗尽
 *      读缓冲区中所有可形成帧的数据。
 *
 * for (;;) {
 *     ret = zerocopy_proceed(ctx);
 *     if (ctx->state == COMPLETE && ret == 0) {
 *         // 成功切割出一个帧，将其传递给下一阶段（聚合器）
 *         feed_to_aggregator(&ctx->outframe);
 *     } else {
 *         // 数据不足(WAITING)或出错，退出循环，等待下一次I/O事件
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

#define  MAX_ARRAY_STACK_DEEP 5
#define  MAX_ARRAY_ELEMENTS_SIZE 5


/**
 * @brief RESP2命令聚合层上下文 (Command Aggregation Layer Context)。
 * @details
 *      这是两阶段解析流水线的第二阶段。如果说 `parser_context` 是“词法分析器”，
 *      那么 `segment_context` 就是“语法分析器”(Syntactic Analyzer)。
 *
 *      它的核心职责是消费上游 `parser_context` 产出的一系列原子帧 (`parser_out`)，
 *      并根据RESP协议的语法规则，将它们组装成一个逻辑上完整的命令。
 *
 * --- 架构角色与数据流 ---
 *
 *      本上下文是一个被动的、事件驱动的状态机。它不直接参与I/O，仅对输入的帧做出响应。
 *      完整的数据处理流水线如下：
 *
 *      [I/O Event] -> read() -> [zerocopy_proceed] --(produces Frame)--> [segment_context]
 *                                                                               |
 *                                                              (is_command_complete?)--YES--> [Business Logic]
 *
 * --- 工作原理 ---
 *
 * 1.  **状态化聚合 (Stateful Aggregation)**:
 *     与分帧器不同，本上下文是强状态化的。它必须记住当前正在构建的命令的结构。
 *     例如，当接收到一个 "*3\r\n" 帧后，它会记录下“当前层级需要3个元素”这一状态。
 *
 * 2.  **显式栈管理 (Explicit Stack Management)**:
 *     `layers` 数组作为一个显式栈，`layers_cursor` 作为栈指针，用于安全地处理
 *     任意深度的嵌套数组。
 *     - 接收到数组头帧 `*N\r\n` -> “入栈”，`layers_cursor` 递增。
 *     - 当前层级元素聚合完毕 -> “出栈”，`layers_cursor` 递减。
 *
 * 3.  **就绪信号 (Readiness Signal)**:
 *     当一个顶层命令被完整聚合后（通常是 `layers_cursor` 归零），`consumed` 标志位
 *     被置为1。这个标志就是给最终业务逻辑的“命令就绪”信号，表明可以安全地消费
 *     这个已聚合的命令。
 *
 * @note `struct element` 的定义是实现此聚合逻辑的关键。它必须被定义，用于在 `layers`
 *       栈中存储每个已接收元素的信息（类型、指向数据的指针和长度）。
 */
struct segment_context {
    unsigned layers_cursor: 3; // max -> 111  >= COMMAND_MAX_ARRAY_DEEP
    unsigned consumed: 1;

    struct layer_state {
        unsigned expect_count;
        unsigned arrive_count;

        struct element {
            protocol_type type;
            size_t len;
            void *data;
        } elements[MAX_ARRAY_ELEMENTS_SIZE];
    } layers[MAX_ARRAY_STACK_DEEP];
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


static inline int segment_proceed(struct segment_context *stx, struct parser_out *outframe) {
    if (stx->consumed) stx->layers_cursor = 0; //游标归位

    protocol_type type = outframe->type;
    // Arrays 帧不是数据帧 它是长度帧栈游标控制
    if (type == ARRAYS) {
        size_t len = outframe->array_len;
        if (len > MAX_ARRAY_ELEMENTS_SIZE || len < 0) {
#ifndef NDEBUG
            syslog(LOG_ERR, "fatal error :MAX_ARRAY_ELEMENTS_SIZE Out");
#endif
            return -EPIPE;
        }
        stx->layers[stx->layers_cursor].expect_count = len;
        stx->layers_cursor++;
    }



segment_completed:
    stx->consumed = 1;
    return 0;
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
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol", ctx->connection->fd);
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
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol", ctx->connection->fd);
                ret = -EPROTO;
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            if (arraylen >= ARRAY_SIZE_MAX) {
                syslog(LOG_WARNING, "[%d]proceed error : invalid number to long of $ protocol", ctx->connection->fd);
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
                    syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol", ctx->connection->fd);
                    ret = -EPROTO;
                    consumed = i + next_crlf_len_end;
                    goto errorpprotocol;
                }
                if (dlen >= BUFFER_SIZE_MAX) {
                    syslog(LOG_WARNING, "[%d]:proceed error : invalid number to long of $ protocol",
                           ctx->connection->fd);
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
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number of $ protocol", ctx->connection->fd);
                ret = -EPROTO;
                consumed = i + next_crlf_len_end;
                goto errorpprotocol;
            }
            if (arraylen >= ARRAY_SIZE_MAX) {
                syslog(LOG_WARNING, "[%d]:proceed error : invalid number to long of $ protocol", ctx->connection->fd);
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
