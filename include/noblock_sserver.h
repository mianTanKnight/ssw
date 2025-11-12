//
// Created by wenshen on 2025/10/9.
//

#ifndef SSW_NOBLOCK_SSERVER_H
#define SSW_NOBLOCK_SSERVER_H
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include "stdlib.h"
#include <sys/epoll.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define BUFFER_SIZE_DEFAULT 1024
#define BUFFER_SIZE_MAX  (1024* 1024 * 1024) // 1G

typedef void (*ufree)(void *);

struct connection_t {
    int fd;
    char *read_buffer;
    long long rb_size;
    long long rb_offset;
    long long rb_cap;
    /**
     * wb_size -> 当前 wb 的大小
     * 它支持扩容刷新 也是被共享的(thead unsafe)
     *
     * wb_limit -> 待发送的内容的大小
     * 通常它是一次性确定
     *
     * wb_offset -> 消费游标
     * 它支持多次消费的关键 它控制从那里开始发送(字符) 到那里止
     *
     */
    char *write_buffer;
    long long wb_cap;
    long long wb_limit;
    long long wb_offset;

    void *use_data;
    ufree use_data_free;
    int flag;
};

/**
 * connections is an array
 * index -> fd
 * v -> connection_t *
 * if currentfd >= size -> expand capacity and move
 */
struct connection_pool {
    struct connection_t **connections;
    size_t size;
    size_t active_count;
};

/*** callback */

typedef int (*on_read_t)(struct connection_t *conn);

typedef int (*on_writer_t)(struct connection_t *conn);

typedef int (*on_error_t)(struct connection_t *conn);

struct runenvironment {
    int sfd;
    struct connection_pool *pool;
    on_read_t on_read;
    on_writer_t on_writer;
    on_error_t on_error;
};


struct connection_pool *create_pool(unsigned init_cap);

int create_connection(struct connection_pool *pool, int fd);

struct connection_t *get_connection(struct connection_pool *pool, int fd);

struct connection_t *take_connection(struct connection_pool *pool, int fd);

int destroy_connection(struct connection_t *connection);

int createsfd(int port, int backlog);

int acceptcfd(int sfd, struct sockaddr_in *caddr, size_t *caddlen);

int setnonblocking(int fd);

int epollrun(struct runenvironment rt);


#endif //SSW_NOBLOCK_SSERVER_H
