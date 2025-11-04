//
// Created by weishen on 2025/11/2.
//

#ifndef SSW_CMD__H
#define SSW_CMD__H
#include <stdio.h>

#include "string.h"

#include "../storage/ohashtable.h"

#define  MAX_KEY_LEN ((1U << 30) -1)
#define  IS_VALID_KEY_LEN(len) ((len) > 0 && (len) <= MAX_KEY_LEN)

typedef void (*free_)(void *);

typedef void * (*malloc_)(size_t size);

/**
 * However, on high-performance links, especially v, it supports big data
 * Then value_len's profits will be very large
 */
struct osv {
    uint64_t vlen;
    char d[];
}__attribute__((aligned(8)));

typedef struct osv osv;

/**
 * 四个基础命令
 * SET
 * GET
 * DEL
 * EXPIRED
 */

static inline int
SET4dup_(const char *key, uint32_t u30keylen, const void *v, uint64_t vlen, const uint32_t expired,
         const malloc_ malloc_func, const free_ free_func) {
#ifndef NDEBUG
    if (!IS_VALID_KEY_LEN(u30keylen))
        return -EINVAL;
#endif
    int ret = 0;
    char *key_dup = malloc_func(u30keylen);
    if (!key_dup) return -ENOMEM;
    memcpy(key_dup, key, u30keylen);
    osv *osv_ = malloc_func(sizeof(osv) + vlen);
    if (!osv_) {
        ret = -ENOMEM;
        goto failure;
    }
    osv_->vlen = vlen;
    memcpy(osv_->d, v, vlen);
    oret_t ot = {0};
    ret = oinsert(key_dup, u30keylen, osv_, expired, &ot);
    if (ret == FULL) {
        if ((ret = expand_capacity(free_func)) < 0)
            goto failure;
        ret = oinsert(key_dup, u30keylen, osv_, expired, &ot);
    }
    if (ret < 0) goto failure;
    if (ret == REPLACED || ret == EXPIRED_) {
        free_func(ot.key);
        free_func(ot.value);
    }
    return ret;
failure:
    free_func(key_dup);
    free_func(osv_);
    return ret;
}


static inline int
SET4dup(const char *key, uint32_t u30keylen, const void *v, uint64_t vlen, const uint32_t expired) {
#ifndef NDEBUG
    if (!IS_VALID_KEY_LEN(u30keylen))
        return -EINVAL;
#endif
    int ret = 0;
    char *key_dup = strndup(key, u30keylen);
    if (!key_dup)
        return -ENOMEM;

    osv *osv_ = malloc(sizeof(osv) + vlen);
    if (!osv_) {
        ret = -ENOMEM;
        goto failure;
    }
    osv_->vlen = vlen;
    memcpy(osv_->d, v, vlen);
    oret_t ot = {0};
    ret = oinsert(key_dup, u30keylen, osv_, expired, &ot);
    if (ret == FULL) {
        if ((ret = expand_capacity(free)) < 0)
            goto failure;
        ret = oinsert(key_dup, u30keylen, osv_, expired, &ot);
    }
    if (ret < 0) goto failure;
    if (ret == REPLACED || ret == EXPIRED_) {
        free(ot.key);
        free(ot.value);
    }
    return ret;
failure:
    free(key_dup);
    free(osv_);
    return ret;
}

static inline osv *
GET(char *key, uint32_t u30keylen) {
#ifndef NDEBUG
    if (!IS_VALID_KEY_LEN(u30keylen))
        return NULL;
#endif
    return oget(key, u30keylen);
}


static inline int
DEL(char *key, uint32_t u30keylen, const free_ free_func) {
#ifndef NDEBUG
    if (!IS_VALID_KEY_LEN(u30keylen))
        return -EINVAL;
#endif
    oret_t ot = {0};
    otake(key, u30keylen, &ot);
    if (ot.key) free_func(ot.key);
    if (ot.value) free_func(ot.value);
    return 0;
}

static inline int
EXPIRED(char *key, uint32_t u30keylen, const uint32_t expired) {
#ifndef NDEBUG
    if (!IS_VALID_KEY_LEN(u30keylen))
        return -EINVAL;
#endif
    oexpired(key, u30keylen, expired);
    return 0;
}

#endif //SSW_CMD__H
