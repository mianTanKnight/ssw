//
// Created by wenshen on 2025/10/9.
//

#include "noblock_sserver.h"

#include <errno.h>


struct connection_pool *create_pool(const unsigned init_cap) {
    if (!init_cap) return NULL;
    struct connection_pool *p = malloc(sizeof(struct connection_pool));
    if (!p) return NULL;
    memset(p, 0, sizeof(struct connection_pool));
    struct connection_t **ct = malloc(sizeof(struct connection_t *) * init_cap);
    if (!ct) {
        free(p);
        return NULL;
    }
    memset(ct, 0, sizeof(struct connection_t *) * init_cap);
    p->connections = ct;
    p->size = init_cap;
    p->active_count = 0;
    return p;
}

int create_connection(struct connection_pool *pool, int fd) {
    if (!pool || fd < 0) return -EINVAL;
    struct connection_t *ct = calloc(sizeof(struct connection_t), 1);
    if (!ct) return -ENOMEM;
    char *rb = NULL;
    char *wb = NULL;
    int ret = 0;
    rb = calloc(BUFFER_SIZE_DEFAULT, 1);
    if (!rb) {
        ret = -ENOMEM;
        goto errorout;
    }
    wb = calloc(BUFFER_SIZE_DEFAULT, 1);
    if (!wb) {
        ret = -ENOMEM;
        goto errorout;
    }
    ct->read_buffer = rb;
    ct->write_buffer = wb;
    ct->rb_cap = BUFFER_SIZE_DEFAULT;
    ct->wb_cap = BUFFER_SIZE_DEFAULT;
    ct->fd = fd;

    if (fd < pool->size) {
        // 直接覆盖 泄漏风险的检测不应该由此函数负责 是调用者的责任
        if (!pool->connections[fd]) pool->active_count++;
        pool->connections[fd] = ct;
        return 0;
    }
    // expand capacity and move of array
    size_t new_cap = pool->size ? pool->size : 1;
    while ((new_cap = new_cap * 2) <= fd) {
    }
    struct connection_t **cts_n = calloc(sizeof(struct connection_t *) * new_cap, 1);
    if (!cts_n) {
        ret = -ENOMEM;
        goto errorout;
    }
    memcpy(cts_n, pool->connections, sizeof(struct connection_t *) * pool->size);
    free(pool->connections);
    cts_n[fd] = ct;
    pool->connections = cts_n;
    pool->size = new_cap;
    // 因为是扩容的 所有 old_ct[fd] 一定不存在
    pool->active_count++;
    return 0;

errorout:
    free(ct);
    free(rb);
    free(wb);
    return ret;
}

struct connection_t *get_connection(struct connection_pool *pool, int fd) {
    if (!pool || fd < 0 || pool->size <= fd) return NULL;
    return pool->connections[fd];
}

struct connection_t *take_connection(struct connection_pool *pool, int fd) {
    if (!pool || fd < 0 || pool->size <= fd) return NULL;
    struct connection_t *ret = pool->connections[fd];
    if (ret) {
        pool->connections[fd] = NULL;
        pool->active_count--;
    }
    return ret;
}

int destroy_connection(struct connection_t *connection) {
    if (connection) {
        free(connection->read_buffer);
        free(connection->write_buffer);
        if (connection->use_data_free) connection->use_data_free(connection->use_data);
        free(connection);
        return 0;
    }
    return -EINVAL;
}


int createsfd(const int port, const int backlog) {
    if (port < 0 || port > 65535 || backlog < 1)
        return -EINVAL;

    struct sockaddr_in server_;
    int sfd, opt = 1;
    memset(&server_, 0, sizeof(server_));

    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "socket() failed : %s", strerror(errno));
        return -errno;
    }

    server_.sin_family = AF_INET;
    server_.sin_port = htons(port);
    server_.sin_addr.s_addr = htonl(INADDR_ANY);


    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(sfd, (struct sockaddr *) &server_, sizeof(server_)) < 0) {
        syslog(LOG_ERR, "bind() failed : %s", strerror(errno));
        close(sfd);
        return -errno;
    }

    if (listen(sfd, backlog) < 0) {
        syslog(LOG_ERR, "listen() failed : %s", strerror(errno));
        close(sfd);
        return -errno;
    }

    return sfd;
}


int acceptcfd(int sfd, struct sockaddr_in *caddr, size_t *caddlen) {
    if (sfd < 0)
        return -EINVAL;
    int cfd;
    struct sockaddr_in c_addr;
    socklen_t c_addr_len = sizeof(c_addr);

    if ((cfd = accept(sfd, (struct sockaddr *) &c_addr, &c_addr_len)) < 0) {
        return -errno;
    }
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &c_addr.sin_addr, client_ip, sizeof(client_ip));
    syslog(LOG_INFO, "Accepted connection from %s:%d (fd=%d)",
           client_ip, ntohs(c_addr.sin_port), cfd);

    if (caddr)
        *caddr = c_addr;
    if (caddlen)
        *caddlen = c_addr_len;

    return cfd;
}


int setnonblocking(int fd) {
    if (fd < 0)
        return -EINVAL;
    int flag = fcntl(fd, F_GETFL, 0);
    if (flag < 0) {
        syslog(LOG_ERR, "fcntl() failed : %s", strerror(errno));
        return -errno;
    }

    if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0) {
        syslog(LOG_ERR, "fcntl(F_SETFL) failed: %s", strerror(errno));
        return -errno;
    }

    return 0;
}

/**
 *  writere 只发送 writerBuffer中的数据(如果存在)
 */
int writere(int efd, const int current_fd, struct connection_t *cn) {
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
     *  char *write_buffer;
     *  size_t wb_size;
     *  size_t wb_limit;
     *  size_t wb_offset;
     */
    if (cn->wb_limit > cn->wb_offset) {
        while (1) {
            // cn->write_buffer + cn->wb_offset:  当次发送的位置
            // cn->wb_limit - cn->wb_offset: 剩余量
            ssize_t current_quantity_sent = send(current_fd, cn->write_buffer + cn->wb_offset,
                                                 cn->wb_limit - cn->wb_offset,MSG_NOSIGNAL);
            if (current_quantity_sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 内核写缓冲区已满 重新注册上写事件 等待下回
                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;
                    ev.data.fd = current_fd;
                    if (epoll_ctl(efd, EPOLL_CTL_MOD, current_fd, &ev) < 0) {
                        syslog(LOG_WARNING, "epoll_ctl() failed : %s", strerror(errno));
                        return -1;
                    }
                    break;
                }
                if (errno == EINTR) {
                    syslog(LOG_WARNING, "send failed of eintr retry: %d", current_fd);
                    continue;
                }
                if (errno == EPIPE) {
                    syslog(LOG_WARNING, "epipe closed : %d", current_fd);
                    return -1;
                }
                // 未知错误
                syslog(LOG_ERR, "send() failed of unknown error  : %s, close %d.", strerror(errno), current_fd);
                return -1; // give up
            }
            if (current_quantity_sent == 0) {
                continue; // retry
            }
            //update offset
            cn->wb_offset += current_quantity_sent;
            if (cn->wb_limit <= cn->wb_offset) {
                //已经发送完所有buffer中的内容
                cn->wb_limit = 0;
                cn->wb_offset = 0;
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                ev.data.fd = current_fd;
                if (epoll_ctl(efd, EPOLL_CTL_MOD, current_fd, &ev) < 0) {
                    syslog(LOG_WARNING, "epoll_ctl() failed : %s", strerror(errno));
                    return -1;
                }
                break;
            }
        }
    }
    return 0;
}

int epollrun(struct runenvironment rt) {
    int sfd = rt.sfd;
    struct connection_pool *pool = rt.pool;

    if (sfd < 0 || !pool) return -EINVAL;
    int ret = setnonblocking(sfd);
    if (ret < 0) return ret;
    int efd = epoll_create1(0);
    if (efd < 0) return efd;
    if (!rt.on_read) return -EINVAL;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // et 边缘模式
    ev.data.fd = sfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev); //add listen

    struct epoll_event events[1024];
    for (;;) {
        int nfds = epoll_wait(efd, events, 1024, -1);

        for (int i = 0; i < nfds; i++) {
            const struct epoll_event ready_e = events[i];
            const int current_fd = ready_e.data.fd;
            // accept event
            if (current_fd == sfd) {
                for (;;) {
                    int cfd = acceptcfd(sfd, NULL, NULL);
                    if (cfd >= 0) {
                        if (create_connection(pool, cfd) < 0) {
                            syslog(LOG_WARNING, "createc() failed : %s", strerror(errno));
                            close(cfd);
                            continue;
                        }
                        setnonblocking(cfd);
                        ev.events = EPOLLIN | EPOLLET;
                        ev.data.fd = cfd;
                        if (epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
                            syslog(LOG_WARNING, "epoll_ctl() failed : %s", strerror(errno));
                            close(cfd);
                        }
                        continue;
                    }

                    if (cfd == -EAGAIN) break;
                    if (cfd == -EINTR) continue;
                    syslog(LOG_WARNING, "ready error : %s", strerror(-cfd));
                    break;
                }
            } else {
                // get connection
                struct connection_t *cn = get_connection(pool, current_fd);
                if (!cn) {
                    syslog(LOG_WARNING, "get_connection() failed : %s", "create_connection failed ?");
                    goto completedfd;
                }
                // read ready
                if (ready_e.events & EPOLLIN) {
                    while (1) {
                        const size_t space = cn->rb_cap - cn->rb_size;
                        if (space < cn->rb_cap / 2) {
                            long long n_space = cn->rb_cap << 1;
                            if (n_space >= BUFFER_SIZE_MAX) {
                                syslog(LOG_ERR, "max size of buffer destroy_connection and close fd: %d", current_fd);
                                goto completedfd;
                            }
                            char *nrb = calloc(n_space, 1);
                            if (!nrb) {
                                syslog(LOG_ERR, "calloc error out of memory! destroy_connection and close fd: %d",
                                       current_fd);
                                cn->flag = -ENOMEM;
                                rt.on_read(cn);
                                goto completedfd;
                            }
                            memcpy(nrb, cn->read_buffer, cn->rb_size);
                            cn->rb_cap = n_space;
                            free(cn->read_buffer);
                            cn->read_buffer = nrb; // update
                        }
                        ssize_t n = read(current_fd, cn->read_buffer + cn->rb_size, cn->rb_cap - cn->rb_size);

                        if (n > 0) {
                            // update count
                            cn->rb_size += n;
                        } else if (n == 0) {
                            // client disconnect
                            syslog(LOG_INFO, "client disconnect fd :%d", current_fd);
                            goto completedfd;
                        } else {
                            // n < 0
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                // 网络是存在拆粘包的问题 所以eagain 的触发点是 内核多缓冲区是否被消费完
                                // 但处理拆粘包的问题并不是epoll的职责 而是on_read()的职责
                                int retofread = rt.on_read(cn); // call on_read_callback
                                //cn 存在 use_data 和 flag 它们影响接下来的 on_writer
                                cn->flag = retofread;
                                if (rt.on_writer) {
                                    rt.on_writer(cn);
                                    if (writere(efd, current_fd, cn) < 0) goto completedfd;
                                }
                                break;
                            }
                            if (errno == EINTR) continue;
                            syslog(LOG_ERR, "read() failed destroy_connection and close fd: %s", strerror(errno));
                            goto completedfd;
                        }
                    }
                }
                // writer ready
                if (ready_e.events & EPOLLOUT && rt.on_writer) {
                    //当写事件被触发 则证明cn写缓冲区是存在数据的
                    if (writere(efd, current_fd, cn) < 0) goto completedfd;
                }
                if (ready_e.events & EPOLLRDHUP) goto completedfd;
                if (ready_e.events & EPOLLERR) {
                    syslog(LOG_ERR, "epoll encounter an unknown error failed: %s", strerror(errno));
                    goto completedfd;
                }
                continue;
            completedfd:
                destroy_connection(take_connection(pool, current_fd));
                epoll_ctl(efd, EPOLL_CTL_DEL, current_fd, NULL);
                close(current_fd);
                break;
            }
        }
    }
}
