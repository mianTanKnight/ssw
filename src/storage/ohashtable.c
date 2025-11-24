//
// Created by wenshen on 2025/10/28.
//

#include "ohashtable.h"

#include <stdio.h>
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

int
expand_capacity(void *free_func) {
    uint64_t n_cap = cap << 1;
#ifndef NDEBUG
    syslog(LOG_INFO, "ohash expand capacity org %" PRIu64 ", new %" PRIu64, cap, n_cap);
    uint64_t migrated = 0, freed = 0;
#endif
    ohash_t *n_ohash = calloc(n_cap * sizeof(ohash_t), 1);
    if (!n_ohash) return -ENOMEM;
    // Start migrating both cap and n_cap, which are powers of 2
    for (uint64_t i = 0; i < cap; i++) {
        /**
        * rm = 0, tb = 0：活跃元素，所有权在哈希表
        * rm = 0, tb = 1：过期元素，所有权仍在哈希表（需要释放）
        * rm = 1, tb = 1：已删除元素，所有权已转移出去（不能释放)
        */
        if (!ohashtabl[i].key && !ohashtabl[i].tb) continue; // NULL Slot

        // Survive
        if (!ohashtabl[i].tb && !ohashtabl[i].rm) {
            uint64_t n_idx = ohashtabl[i].hash & (n_cap - 1);
            while (1) {
                //There is no tombstone because it is new
                if (!n_ohash[n_idx].hash) {
                    memcpy(n_ohash + n_idx, ohashtabl + i, sizeof(ohash_t));
#ifndef NDEBUG
                    migrated++;
#endif
                    break;
                }
                n_idx = (n_idx + 1) & (n_cap - 1);
            }
        }
        //Die due to expiration
        if (ohashtabl[i].tb && !ohashtabl[i].rm && free_func) {
#ifndef NDEBUG
            freed++;
#endif
            ((void (*)(void *)) free_func)(ohashtabl[i].key);
            ((void (*)(void *)) free_func)(ohashtabl[i].v);
        }
    }
#ifndef NDEBUG
    syslog(LOG_INFO, "expansion complete: migrated %" PRIu64 ", freed %" PRIu64,
           migrated, freed);
#endif
    free(ohashtabl);
    ohashtabl = n_ohash;
    cap = n_cap;
    return OK;
}

int
oinsert(char *key, uint32_t keylen, void *v, uint32_t expira, oret_t *oret) {
    if (size * LOAD_FACTOR_DENOMINATOR >= cap * LOAD_FACTOR_THRESHOLD) return FULL;
    int ret = OK;
    uint64_t hash = XXH64(key, keylen, H_SEED);
    uint64_t idx = hash & (cap - 1); // cap is 2 power
    // linear addressing of load-factor is 0.7
    uint64_t icap = cap;
    while (icap--) {
        if (!ohashtabl[idx].key || ohashtabl[idx].rm) {
            if (ohashtabl[idx].rm) ret = REMOVED;
            goto gotoinsert;
        }
        if (ohashtabl[idx].tb) {
            if (oret) {
                oret->key = ohashtabl[idx].key;
                oret->value = ohashtabl[idx].v;
            }
            ret = EXPIRED_;
            goto gotoinsert;
        }
        if (hash == ohashtabl[idx].hash && keylen == ohashtabl[idx].keylen) {
            if (!memcmp(key, ohashtabl[idx].key, keylen)) {
                if (oret) {
                    oret->key = ohashtabl[idx].key;
                    oret->value = ohashtabl[idx].v;
                }
                ret = REPLACED;
                goto gotoinsert;
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
    if (ret == OK || ret == REMOVED) size++;
    return ret;
}


void *
oget(char *key, uint32_t keylen) {
    long sec = get_current_time_seconds();
    uint64_t hash = XXH64(key, keylen, H_SEED);
    uint64_t idx = hash & (cap - 1); // cap is 2 power
    uint64_t icap = cap;
    while (icap--) {
        if (!ohashtabl[idx].key && !ohashtabl[idx].tb)
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
        if (!ohashtabl[idx].key && !ohashtabl[idx].tb)
            return;
        if (ohashtabl[idx].tb)
            goto next;
        if (hash == ohashtabl[idx].hash && keylen == ohashtabl[idx].keylen) {
            if (!memcmp(key, ohashtabl[idx].key, keylen)) {
                ohashtabl[idx].rm = 1;
                ohashtabl[idx].tb = 1;
                oret->key = ohashtabl[idx].key;
                oret->value = ohashtabl[idx].v;
                ohashtabl[idx].key = NULL;
                ohashtabl[idx].v = NULL;
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


void oexpired(char *key, uint32_t keylen, uint32_t expiratime) {
    long sec = get_current_time_seconds();
    uint64_t hash = XXH64(key, keylen, H_SEED);
    uint64_t idx = hash & (cap - 1);
    uint64_t icap = cap;
    while (icap--) {
        if (!ohashtabl[idx].key && !ohashtabl[idx].tb)
            return;
        if (ohashtabl[idx].tb)
            goto next;
        if (hash == ohashtabl[idx].hash && keylen == ohashtabl[idx].keylen) {
            if (!memcmp(key, ohashtabl[idx].key, keylen)) {
                ohashtabl[idx].expiratime = expiratime;
                return;
            }
        }
        if (ohashtabl[idx].expiratime > 0 && sec >= ohashtabl[idx].expiratime)
            ohashtabl[idx].tb = 1; // tombstone,without any deletions
    next:
        idx = (idx + 1) & (cap - 1);
    }
}
