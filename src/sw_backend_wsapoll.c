#include "sw_backend.h"

#if !defined(_WIN32)
#error "The WSAPoll backend is only available on Windows."
#endif

#include <stdlib.h>

struct sw_backend {
    int unused;
};

int sw_backend_init(sw_mgr* mgr) {
    mgr->backend = (sw_backend*)calloc(1, sizeof(*mgr->backend));
    return (mgr->backend != NULL) ? 0 : -1;
}

void sw_backend_shutdown(sw_mgr* mgr) {
    if (mgr == NULL) {
        return;
    }
    free(mgr->backend);
    mgr->backend = NULL;
}

int sw_backend_register_listener(sw_mgr* mgr, sw_listener* listener) {
    (void)mgr;
    (void)listener;
    return 0;
}

int sw_backend_register_connection(sw_mgr* mgr, sw_connection* connection) {
    (void)mgr;
    (void)connection;
    return 0;
}

int sw_backend_update_connection(sw_mgr* mgr, sw_connection* connection) {
    (void)mgr;
    (void)connection;
    return 0;
}

void sw_backend_unregister_listener(sw_mgr* mgr, sw_listener* listener) {
    (void)mgr;
    (void)listener;
}

void sw_backend_unregister_connection(sw_mgr* mgr, sw_connection* connection) {
    (void)mgr;
    (void)connection;
}

int sw_backend_poll(sw_mgr* mgr, i32 timeout_ms) {
    const sz listener_count = s_array_get_size(&mgr->listeners);
    const sz connection_count = s_array_get_size(&mgr->connections);
    const sz total_count = listener_count + connection_count;
    WSAPOLLFD* fds;
    void** sources;
    int ready_count;
    sz i;

    if (total_count == 0) {
        Sleep((DWORD)(timeout_ms > 0 ? timeout_ms : 0));
        return 0;
    }

    fds = (WSAPOLLFD*)calloc(total_count, sizeof(*fds));
    sources = (void**)calloc(total_count, sizeof(*sources));
    if (fds == NULL || sources == NULL) {
        free(fds);
        free(sources);
        return -1;
    }

    for (i = 0; i < listener_count; ++i) {
        sw_listener* listener = s_array_get_data(&mgr->listeners)[i];
        fds[i].fd = listener->fd;
        fds[i].events = POLLRDNORM;
        sources[i] = listener;
    }

    for (i = 0; i < connection_count; ++i) {
        sw_connection* connection = s_array_get_data(&mgr->connections)[i];
        const sz index = listener_count + i;
        fds[index].fd = connection->fd;
        if (sw_connection_wants_read(connection)) {
            fds[index].events |= POLLRDNORM;
        }
        if (sw_connection_wants_write(connection)) {
            fds[index].events |= POLLWRNORM;
        }
        sources[index] = connection;
    }

    ready_count = WSAPoll(fds, (ULONG)total_count, timeout_ms);
    if (ready_count == SOCKET_ERROR) {
        free(fds);
        free(sources);
        return -1;
    }

    for (i = 0; i < total_count; ++i) {
        if (fds[i].revents == 0) {
            continue;
        }

        if (*(sw_source_kind*)sources[i] == SW_SOURCE_LISTENER) {
            if (sw_mgr_accept_ready(mgr, (sw_listener*)sources[i]) < 0) {
                free(fds);
                free(sources);
                return -1;
            }
            continue;
        }

        if ((fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            sw_mgr_close_connection(mgr, (sw_connection*)sources[i]);
            continue;
        }

        if ((fds[i].revents & POLLRDNORM) != 0) {
            if (sw_mgr_connection_readable(mgr, (sw_connection*)sources[i]) < 0) {
                continue;
            }
        }

        if ((fds[i].revents & POLLWRNORM) != 0) {
            if (sw_mgr_connection_writable(mgr, (sw_connection*)sources[i]) < 0) {
                continue;
            }
        }
    }

    free(fds);
    free(sources);
    return ready_count;
}
