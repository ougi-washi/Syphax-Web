#include "sw_backend.h"

#if !defined(__linux__)
#error "The epoll backend is only available on Linux."
#endif

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

struct sw_backend {
    int epoll_fd;
};

static uint32_t sw_backend_connection_events(const sw_connection* connection) {
    uint32_t events = EPOLLRDHUP;
    if (sw_connection_wants_read(connection)) {
        events |= EPOLLIN;
    }
    if (sw_connection_wants_write(connection)) {
        events |= EPOLLOUT;
    }
    return events;
}

int sw_backend_init(sw_mgr* mgr) {
    mgr->backend = (sw_backend*)calloc(1, sizeof(*mgr->backend));
    if (mgr->backend == NULL) {
        return -1;
    }
    mgr->backend->epoll_fd = epoll_create1(0);
    if (mgr->backend->epoll_fd < 0) {
        free(mgr->backend);
        mgr->backend = NULL;
        return -1;
    }
    return 0;
}

void sw_backend_shutdown(sw_mgr* mgr) {
    if (mgr == NULL || mgr->backend == NULL) {
        return;
    }
    close(mgr->backend->epoll_fd);
    free(mgr->backend);
    mgr->backend = NULL;
}

int sw_backend_register_listener(sw_mgr* mgr, sw_listener* listener) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.ptr = listener;
    return epoll_ctl(mgr->backend->epoll_fd, EPOLL_CTL_ADD, listener->fd, &event);
}

int sw_backend_register_connection(sw_mgr* mgr, sw_connection* connection) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = sw_backend_connection_events(connection);
    event.data.ptr = connection;
    return epoll_ctl(mgr->backend->epoll_fd, EPOLL_CTL_ADD, connection->fd, &event);
}

int sw_backend_update_connection(sw_mgr* mgr, sw_connection* connection) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = sw_backend_connection_events(connection);
    event.data.ptr = connection;
    return epoll_ctl(mgr->backend->epoll_fd, EPOLL_CTL_MOD, connection->fd, &event);
}

void sw_backend_unregister_listener(sw_mgr* mgr, sw_listener* listener) {
    if (mgr == NULL || mgr->backend == NULL || listener == NULL || listener->fd == SW_INVALID_SOCKET) {
        return;
    }
    epoll_ctl(mgr->backend->epoll_fd, EPOLL_CTL_DEL, listener->fd, NULL);
}

void sw_backend_unregister_connection(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || mgr->backend == NULL || connection == NULL || connection->fd == SW_INVALID_SOCKET) {
        return;
    }
    epoll_ctl(mgr->backend->epoll_fd, EPOLL_CTL_DEL, connection->fd, NULL);
}

int sw_backend_poll(sw_mgr* mgr, i32 timeout_ms) {
    struct epoll_event events[64];
    int count = epoll_wait(mgr->backend->epoll_fd, events, 64, timeout_ms);
    int i;

    if (count < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    for (i = 0; i < count; ++i) {
        void* source = events[i].data.ptr;
        if (source == NULL) {
            continue;
        }

        if (*(sw_source_kind*)source == SW_SOURCE_LISTENER) {
            if (sw_mgr_accept_ready(mgr, (sw_listener*)source) < 0) {
                return -1;
            }
            continue;
        }

        if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            sw_mgr_close_connection(mgr, (sw_connection*)source);
            continue;
        }

        if ((events[i].events & EPOLLIN) != 0) {
            if (sw_mgr_connection_readable(mgr, (sw_connection*)source) < 0) {
                continue;
            }
        }

        if ((events[i].events & EPOLLOUT) != 0) {
            if (sw_mgr_connection_writable(mgr, (sw_connection*)source) < 0) {
                continue;
            }
        }
    }

    return count;
}
