//
// Created by wenshen on 2025/10/28.
//

#include "ohashtable.h"

#include <string.h>
ohash_t *ohashtabl = NULL;
uint64_t cap = 0;
uint64_t size = 0;

int
initohash(uint64_t cap_) {
    if (cap_ & cap_ - 1)
        cap_ = getnext2power(cap_);
    ohash_t *oht = calloc(cap_ * sizeof(ohash_t), 1);
    if (!oht) return -ENOMEM;
    ohashtabl = oht;
    cap = cap_;
    return OK;
}

int expand_capacity(free_ free) {
    uint64_t n_cap = cap << 1;
#ifndef NDEBUG
    syslog(LOG_INFO, "ohash expand capacity org %" PRIu64 ", new %" PRIu64, cap, n_cap);
#endif
    ohash_t *n_ohash = calloc(n_cap * sizeof(ohash_t), 1);
    if (!n_ohash) return -ENOMEM;
    // Start migrating both cap and n_cap, which are powers of 2
    for (uint64_t i = 0; i < cap; i++) {
        if (!ohashtabl[i].key) continue;
        // 所有权已归还（rm = 1），跳过
        if (ohashtabl[i].rm) continue;
        // live element of live
        if (!ohashtabl[i].tb && ohashtabl[i].key) {
            uint64_t n_idx = ohashtabl[i].hash & (n_cap - 1);
            while (1) {
                //There is no tombstone because it is new
                if (!n_ohash[n_idx].hash) {
                    memcpy(n_ohash + n_idx, ohashtabl + i, sizeof(ohash_t));
                    break;
                }
                n_idx = (n_idx + 1) & (n_cap - 1);
            }
            continue;
        }
        if (free) {
            free(ohashtabl[i].key);
            free(ohashtabl[i].v);
        }
    }
    free(ohashtabl);
    ohashtabl = n_ohash;
    cap = n_cap;
    return OK;
}

int
oinsert(char *key, uint32_t keylen, void *v, int expira, oret_t *oret) {
    if (size * LOAD_FACTOR_DENOMINATOR >= cap * LOAD_FACTOR_THRESHOLD)
        return FULL;

    uint64_t hash = XXH64(key, keylen, H_SEED);
    uint64_t idx = hash & (cap - 1); // cap is 2 power
    // linear addressing of load-factor is 0.7
    uint64_t icap = cap;
    while (icap--) {
        if (ohashtabl[idx].tb) {
            // 返回所有权
            if (oret) {
                oret->key = ohashtabl[idx].key;
                oret->value = ohashtabl[idx].v;
            }
            goto gotoinsert;
        }
        if (!ohashtabl[idx].key) goto gotoinsert;
        if (hash == ohashtabl[idx].hash && keylen == ohashtabl[idx].keylen) {
            if (!memcmp(key, ohashtabl[idx].key, keylen)) {
                // 接受value 的所有权 并返回旧value的控制权
                // key 默认拒绝接受 当 *retv 不为null时 外部需要自行处理key
                if (oret) {
                    oret->key = key;
                    oret->value = ohashtabl[idx].v;
                }
                ohashtabl[idx].v = v;
                ohashtabl[idx].expiratime = expira;
                return REPLACED;
            }
        }
        idx = (idx + 1) & (cap - 1);
    }
    return UNKNOWN_ERROR;
gotoinsert:
    ohashtabl[idx].hash = hash;
    // 所有权转移 table 并不会支持分配和释放 它只负责管理所有权
    ohashtabl[idx].key = key;
    ohashtabl[idx].v = v;
    ohashtabl[idx].keylen = keylen;
    ohashtabl[idx].expiratime = expira;
    ohashtabl[idx].tb = 0;
    ohashtabl[idx].rm = 0;
    size++;
    return OK;
}


void *
oget(char *key, uint32_t keylen) {
    long sec = get_current_time_seconds();
    uint64_t hash = XXH64(key, keylen, H_SEED);
    uint64_t idx = hash & (cap - 1); // cap is 2 power
    uint64_t icap = cap;
    while (icap--) {
        if (!ohashtabl[idx].key)
            goto notfound;
        if (ohashtabl[idx].tb)
            goto next;
        if (hash == ohashtabl[idx].hash && keylen == ohashtabl[idx].keylen) {
            if (ohashtabl[idx].expiratime > 0 && sec >= ohashtabl[idx].expiratime)
                goto expire;
            if (!memcmp(key, ohashtabl[idx].key, keylen))
                return ohashtabl[idx].v;
        }
        if (ohashtabl[idx].expiratime > 0 && sec >= ohashtabl[idx].expiratime)
            ohashtabl[idx].tb = 1; // tombstone,without any deletions
    next:
        idx = (idx + 1) & (cap - 1);
    }
notfound:
    return NULL;
expire:
    ohashtabl[idx].tb = 1; // tombstone,without any deletions
    return NULL;
}

void
otake(char *key, uint32_t keylen, oret_t *oret) {
    long sec = get_current_time_seconds();
    uint64_t hash = XXH64(key, keylen, H_SEED);
    uint64_t idx = hash & (cap - 1); // cap is 2 power
    uint64_t icap = cap;
    while (icap--) {
        if (!ohashtabl[idx].key)
            return;
        if (ohashtabl[idx].tb)
            goto next;
        if (hash == ohashtabl[idx].hash && keylen == ohashtabl[idx].keylen) {
            if (!memcmp(key, ohashtabl[idx].key, keylen)) {
                ohashtabl[idx].rm = 1;
                ohashtabl[idx].tb = 1;
                oret->key = ohashtabl[idx].key;
                oret->value = ohashtabl[idx].v;
                size--;
                break;
            }
        }
        if (ohashtabl[idx].expiratime > 0 && sec >= ohashtabl[idx].expiratime)
            ohashtabl[idx].tb = 1; // tombstone,without any deletions
    next:
        idx = (idx + 1) & (cap - 1);
    }
}
