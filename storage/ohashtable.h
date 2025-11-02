//
// Created by wenshen on 2025/10/28.
//

#ifndef SSW_OHASHTABLE_H
#define SSW_OHASHTABLE_H
#include "xxhash.h"
#include "stdlib.h"
#include "syslog.h"
#include "errno.h"
#include "time.h"
#include "inttypes.h"
/**
 * Open Addressing Hash Table of unsafe thread
 * open source xxhash of hash func
 *
 * Use Linear Addressing
 * Capacity: m = 2^k (power of 2)
 * Step size: c (must be odd to ensure gcd(c, m) = 1)
 *
 */
#define LOAD_FACTOR_THRESHOLD 7
#define LOAD_FACTOR_DENOMINATOR 10
#define H_SEED 20231027


typedef void (*free_)(void *);

typedef enum {
    OK = 0,
    REPLACED = 1,
    FULL = -1,
    UNKNOWN_ERROR = -2,
} orets;

/**
 * osv 在ohash中 几乎不会被访问
 * 但在高性能链路上 特别是v是支持大数据
 * 那么 vlen的收益 将非常大
 *
 */
struct osv {
    uint64_t vlen;
    char d[];
}__attribute__((aligned(8)));

typedef struct osv osv;

/**
 * CRITICAL DESIGN CONSTRAINT:
 * sizeof(ohash_t) MUST BE EXACTLY 32 BYTES
 *
 * Why 32?
 * - CPU cache line = 64 bytes
 * - 64 / 32 = 2 perfect slots per cache line
 * - Linear probing gets 2 slots in ONE memory fetch
 * - This is the FOUNDATION of our performance
 */
struct ohash_t {
    uint64_t hash; // 8 字节
    char *key; // 8 字节
    osv *v; // 8 字节
    uint32_t tb: 1;
    uint32_t rm: 1; // is removed
    uint32_t keylen: 30;
    uint32_t expiratime; // seconds
} __attribute__((aligned(8)));


struct oret_t {
    char *key;
    osv *value;
} __attribute__((aligned(8)));

typedef struct ohash_t ohash_t;
typedef struct oret_t oret_t;

/**
 * ohash_t 并不需要create
 * 全局唯一
 */
extern ohash_t *ohashtabl;
extern uint64_t cap;
extern uint64_t size;

static inline time_t get_current_time_seconds(void) {
    return time(NULL);
}

static inline uint64_t getnext2power(uint64_t i) {
    i |= i >> 1;
    i |= i >> 2;
    i |= i >> 4;
    i |= i >> 8;
    i |= i >> 16;
    i |= i >> 32;
    return i + 1;
}


/**
 * OWNERSHIP CONTRACT:
 *
 * oinsert(key, value, ...):
 *   - TAKES ownership of key and value
 *   - Caller must NOT free them after call
 *   - On REPLACED, returns old value via oret
 *
 * oget(key, ...):
 *   - BORROWS: returns pointer, does NOT transfer ownership
 *   - Caller must NOT free the returned pointer
 *   - Pointer valid until next otake() or table destroy
 *
 * otake(key, oret):
 *   - RETURNS ownership of key and value via oret
 *   - Caller MUST free oret->key and oret->value
 *   - Sets tombstone, does NOT access pointers afterward
 *
 * MEMORY SAFETY:
 *   - Tombstone pointers are dangling but never dereferenced
 *   - Expansion copies only live entries, tombstones discarded
 */
int initohash(uint64_t cap_);

int oinsert(char *key, uint32_t keylen, osv *v, uint32_t expira, oret_t *oret);

/**
 * Retrieves a value by key.
 * NOTE: This function also performs lazy deletion of any expired items
 * encountered during the search probe.
 */
osv *oget(char *key, uint32_t keylen);

void otake(char *key, uint32_t keylen, oret_t *oret);

void oexpired(char *key, uint32_t keylen, uint32_t expiratime);

/**
 *  expand_capacity is an authorization action
 *  ohash will notify decision-makers whether expansion is needed when it is full
 *  And how to handle expired elements (free function)
 *  ohash does not violate the principle of "who creates, who destroys"
 */
int expand_capacity(free_ free);


#endif //SSW_OHASHTABLE_H
