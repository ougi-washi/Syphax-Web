#include "sw_server.h"
#include "syphax/s_thread.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#    error "server_stress is currently a POSIX stress tool."
#else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/epoll.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

typedef struct {
    char mode[32];
    sz connections;
    sz requests;
    sz workers;
    sz large_bytes;
    i32 duration_ms;
} stress_options;

typedef struct {
    char* large_body;
    sz large_body_len;
} stress_state;

typedef struct {
    double* values;
    sz count;
    sz capacity;
} latency_list;

typedef struct {
    int fd;
    b8 open;
    b8 waiting;
    char response[512];
    sz response_len;
    double sent_at_ms;
} spam_connection;

static double stress_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}

static long stress_rss_bytes(void) {
    FILE* file = fopen("/proc/self/statm", "rb");
    long pages = 0;
    long page_size = sysconf(_SC_PAGESIZE);

    if (file == NULL || page_size <= 0) {
        if (file != NULL) {
            fclose(file);
        }
        return -1;
    }
    if (fscanf(file, "%*s %ld", &pages) != 1) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return pages * page_size;
}

static void stress_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    stress_state* state = (stress_state*)user_data;

    if (sw_http_is(request, "GET", "/large")) {
        (void)sw_http_reply(connection, 200, "application/octet-stream", state->large_body, state->large_body_len);
        return;
    }
    if (sw_http_is(request, "GET", "/slowloris-ok")) {
        (void)sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "ok\n");
        return;
    }
    (void)sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "ok\n");
}

static int stress_connect(u16 port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;

    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int stress_set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int stress_connect_many_source(u16 port, sz index) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in source;
    struct sockaddr_in target;
    const int nodelay = 1;
    const u32 source_ip = 0x7f000001u + (u32)(index / 20000u);

    if (fd < 0) {
        return -1;
    }

    memset(&source, 0, sizeof(source));
    source.sin_family = AF_INET;
    source.sin_port = 0;
    source.sin_addr.s_addr = htonl(source_ip);
    if (bind(fd, (struct sockaddr*)&source, sizeof(source)) != 0) {
        close(fd);
        return -1;
    }

    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&target, sizeof(target)) != 0) {
        close(fd);
        return -1;
    }

    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    if (stress_set_nonblocking(fd) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int stress_send_all(int fd, const char* data, sz len) {
    sz offset = 0;

    while (offset < len) {
        const sz remaining = len - offset;
        const ssize_t sent = send(fd, data + offset, remaining > (sz)INT_MAX ? INT_MAX : (int)remaining, 0);
        if (sent <= 0) {
            return -1;
        }
        offset += (sz)sent;
    }
    return 0;
}

static sz stress_content_length(const char* response) {
    const char* header = strstr(response, "Content-Length:");

    if (header == NULL) {
        return 0;
    }
    header += strlen("Content-Length:");
    while (*header == ' ') {
        ++header;
    }
    return (sz)strtoull(header, NULL, 10);
}

static int stress_read_response(int fd, sz* out_bytes) {
    char buffer[8192];
    char* response = NULL;
    sz response_len = 0;
    sz response_cap = 0;
    sz expected_body = 0;
    sz header_len = 0;
    int complete = 0;

    if (out_bytes != NULL) {
        *out_bytes = 0;
    }

    while (!complete) {
        const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        char* header_end;

        if (received <= 0) {
            free(response);
            return -1;
        }
        if (response_len + (sz)received + 1 > response_cap) {
            sz next_cap = response_cap > 0 ? response_cap * 2 : 16384;
            char* next_response;
            while (next_cap < response_len + (sz)received + 1) {
                next_cap *= 2;
            }
            next_response = (char*)realloc(response, next_cap);
            if (next_response == NULL) {
                free(response);
                return -1;
            }
            response = next_response;
            response_cap = next_cap;
        }
        memcpy(response + response_len, buffer, (sz)received);
        response_len += (sz)received;
        response[response_len] = '\0';

        header_end = strstr(response, "\r\n\r\n");
        if (header_end != NULL) {
            header_len = (sz)(header_end + 4 - response);
            expected_body = stress_content_length(response);
            complete = response_len >= header_len + expected_body;
        }
    }

    if (out_bytes != NULL) {
        *out_bytes = response_len;
    }
    free(response);
    return 0;
}

static b8 spam_response_complete(spam_connection* connection) {
    char* header_end;
    sz header_len;
    sz body_len;

    if (connection == NULL) {
        return 0;
    }
    connection->response[connection->response_len < sizeof(connection->response)
        ? connection->response_len
        : sizeof(connection->response) - 1] = '\0';
    header_end = strstr(connection->response, "\r\n\r\n");
    if (header_end == NULL || strstr(connection->response, "Content-Length:") == NULL) {
        return 0;
    }
    header_len = (sz)(header_end + 4 - connection->response);
    body_len = stress_content_length(connection->response);
    return connection->response_len >= header_len + body_len;
}

static void latency_push(latency_list* list, double value) {
    if (list->count + 1 > list->capacity) {
        const sz next_capacity = list->capacity > 0 ? list->capacity * 2 : 1024;
        double* next_values = (double*)realloc(list->values, next_capacity * sizeof(*next_values));
        assert(next_values != NULL);
        list->values = next_values;
        list->capacity = next_capacity;
    }
    list->values[list->count++] = value;
}

static int compare_double(const void* lhs, const void* rhs) {
    const double a = *(const double*)lhs;
    const double b = *(const double*)rhs;
    return (a > b) - (a < b);
}

static double latency_percentile(latency_list* list, double percentile) {
    sz index;

    if (list->count == 0) {
        return 0.0;
    }
    qsort(list->values, list->count, sizeof(*list->values), compare_double);
    index = (sz)((percentile / 100.0) * (double)(list->count - 1));
    if (index >= list->count) {
        index = list->count - 1;
    }
    return list->values[index];
}

static void print_summary(sw_server* server, const stress_options* options, latency_list* latencies, double elapsed_ms, sz failures, long rss_before, long rss_after) {
    sz worker_index;
    const double seconds = elapsed_ms / 1000.0;
    const double requests_per_sec = seconds > 0.0 ? (double)latencies->count / seconds : 0.0;
    const long rss_delta = (rss_before >= 0 && rss_after >= 0) ? rss_after - rss_before : -1;
    const double memory_per_connection = (rss_delta > 0 && options->connections > 0)
        ? (double)rss_delta / (double)options->connections
        : 0.0;

    printf("mode=%s\n", options->mode);
    printf("requests=%zu\n", latencies->count);
    printf("requests_per_sec=%.2f\n", requests_per_sec);
    printf("p50_ms=%.3f\n", latency_percentile(latencies, 50.0));
    printf("p95_ms=%.3f\n", latency_percentile(latencies, 95.0));
    printf("p99_ms=%.3f\n", latency_percentile(latencies, 99.0));
    printf("open_connections=%zu\n", sw_server_open_connections(server));
    printf("failed_connects=%zu\n", failures);
    printf("rss_delta_bytes=%ld\n", rss_delta);
    printf("memory_per_connection_bytes=%.2f\n", memory_per_connection);
    printf("worker_count=%zu\n", sw_server_worker_count(server));
    for (worker_index = 0; worker_index < sw_server_worker_count(server); ++worker_index) {
        printf("worker_%zu_accepted=%zu\n", worker_index, sw_server_worker_accepted_connections(server, worker_index));
    }
}

static sz run_request_loop(u16 port, const char* path, sz requests, latency_list* latencies) {
    sz failures = 0;
    sz i;

    for (i = 0; i < requests; ++i) {
        char request[256];
        int fd = stress_connect(port);
        double start_ms;

        if (fd < 0) {
            failures += 1;
            continue;
        }
        snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", path);
        start_ms = stress_now_ms();
        if (stress_send_all(fd, request, strlen(request)) != 0 || stress_read_response(fd, NULL) != 0) {
            failures += 1;
        } else {
            latency_push(latencies, stress_now_ms() - start_ms);
        }
        close(fd);
    }
    return failures;
}

static sz run_idle(u16 port, const stress_options* options, long* rss_after_open) {
    int* sockets = (int*)calloc(options->connections, sizeof(*sockets));
    sz failures = 0;
    sz i;

    assert(sockets != NULL);
    for (i = 0; i < options->connections; ++i) {
        sockets[i] = stress_connect(port);
        if (sockets[i] < 0) {
            failures += 1;
        }
    }
    s_thread_sleep_ms((u32)options->duration_ms);
    if (rss_after_open != NULL) {
        *rss_after_open = stress_rss_bytes();
    }
    for (i = 0; i < options->connections; ++i) {
        if (sockets[i] >= 0) {
            close(sockets[i]);
        }
    }
    free(sockets);
    return failures;
}

static sz run_keepalive_mixed(u16 port, const stress_options* options, latency_list* latencies, long* rss_after_open) {
    int* idle_sockets = (int*)calloc(options->connections, sizeof(*idle_sockets));
    sz failures = 0;
    sz i;
    int active_fd;

    assert(idle_sockets != NULL);
    for (i = 0; i < options->connections; ++i) {
        idle_sockets[i] = stress_connect(port);
        if (idle_sockets[i] < 0) {
            failures += 1;
        }
    }
    if (rss_after_open != NULL) {
        *rss_after_open = stress_rss_bytes();
    }

    active_fd = stress_connect(port);
    if (active_fd < 0) {
        failures += options->requests;
    } else {
        for (i = 0; i < options->requests; ++i) {
            const char request[] = "GET /small HTTP/1.1\r\nHost: localhost\r\n\r\n";
            const double start_ms = stress_now_ms();
            if (stress_send_all(active_fd, request, sizeof(request) - 1) != 0 || stress_read_response(active_fd, NULL) != 0) {
                failures += 1;
                break;
            }
            latency_push(latencies, stress_now_ms() - start_ms);
        }
        close(active_fd);
    }

    for (i = 0; i < options->connections; ++i) {
        if (idle_sockets[i] >= 0) {
            close(idle_sockets[i]);
        }
    }
    free(idle_sockets);
    return failures;
}

static sz run_slowloris(u16 port, const stress_options* options, latency_list* latencies) {
    sz failures = 0;
    sz i;

    for (i = 0; i < options->connections; ++i) {
        int fd = stress_connect(port);
        const char partial[] = "GET /slowloris-ok HTTP/1.1\r\nHost: localhost\r\nX-Slow: ";
        char byte = 'a';
        const double start_ms = stress_now_ms();

        if (fd < 0) {
            failures += 1;
            continue;
        }
        if (stress_send_all(fd, partial, sizeof(partial) - 1) != 0) {
            failures += 1;
            close(fd);
            continue;
        }
        s_thread_sleep_ms((u32)options->duration_ms);
        (void)send(fd, &byte, 1, 0);
        if (stress_read_response(fd, NULL) == 0) {
            latency_push(latencies, stress_now_ms() - start_ms);
        }
        close(fd);
    }
    return failures;
}

static sz spam_send_request(spam_connection* connection, latency_list* latencies, sz* total_sent, sz max_requests) {
    static const char request[] = "GET /small HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const ssize_t sent = send(connection->fd, request, sizeof(request) - 1, 0);

    if (sent == (ssize_t)(sizeof(request) - 1)) {
        connection->waiting = 1;
        connection->response_len = 0;
        connection->sent_at_ms = stress_now_ms();
        *total_sent += 1;
        (void)latencies;
        (void)max_requests;
        return 0;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return 1;
}

static sz run_spam(u16 port, const stress_options* options, latency_list* latencies, long* rss_after_open) {
    const int epoll_fd = epoll_create1(0);
    struct epoll_event* events;
    spam_connection* connections;
    sz failures = 0;
    sz open_count = 0;
    sz total_sent = 0;
    sz i;
    double deadline_ms;
    const sz max_requests = options->requests;

    assert(epoll_fd >= 0);
    events = (struct epoll_event*)calloc(4096, sizeof(*events));
    connections = (spam_connection*)calloc(options->connections, sizeof(*connections));
    assert(events != NULL);
    assert(connections != NULL);

    for (i = 0; i < options->connections; ++i) {
        struct epoll_event event;
        connections[i].fd = stress_connect_many_source(port, i);
        if (connections[i].fd < 0) {
            failures += 1;
            continue;
        }
        connections[i].open = 1;
        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
        event.data.u64 = (u64)i;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connections[i].fd, &event) != 0) {
            close(connections[i].fd);
            connections[i].open = 0;
            failures += 1;
            continue;
        }
        open_count += 1;
        if ((i + 1) % 10000 == 0) {
            fprintf(stderr, "opened=%zu failed_connects=%zu\n", i + 1, failures);
        }
    }

    if (rss_after_open != NULL) {
        *rss_after_open = stress_rss_bytes();
    }
    deadline_ms = stress_now_ms() + (double)options->duration_ms;

    while (open_count > 0
        && stress_now_ms() < deadline_ms
        && (max_requests == 0 || latencies->count < max_requests)) {
        const int ready = epoll_wait(epoll_fd, events, 4096, 50);
        int event_index;

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            failures += open_count;
            break;
        }

        for (event_index = 0; event_index < ready; ++event_index) {
            spam_connection* connection;
            sz index = (sz)events[event_index].data.u64;

            if (index >= options->connections) {
                continue;
            }
            connection = &connections[index];
            if (!connection->open) {
                continue;
            }

            if ((events[event_index].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                close(connection->fd);
                connection->open = 0;
                open_count -= 1;
                failures += 1;
                continue;
            }

            if ((events[event_index].events & EPOLLIN) != 0 && connection->waiting) {
                for (;;) {
                    char chunk[256];
                    const ssize_t received = recv(connection->fd, chunk, sizeof(chunk), 0);
                    if (received > 0) {
                        const sz copy_len = connection->response_len + (sz)received < sizeof(connection->response) - 1
                            ? (sz)received
                            : sizeof(connection->response) - 1 - connection->response_len;
                        if (copy_len > 0) {
                            memcpy(connection->response + connection->response_len, chunk, copy_len);
                            connection->response_len += copy_len;
                            connection->response[connection->response_len] = '\0';
                        }
                        if (spam_response_complete(connection)) {
                            latency_push(latencies, stress_now_ms() - connection->sent_at_ms);
                            connection->waiting = 0;
                            connection->response_len = 0;
                            break;
                        }
                        continue;
                    }
                    if (received == 0) {
                        close(connection->fd);
                        connection->open = 0;
                        open_count -= 1;
                        failures += 1;
                        break;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    close(connection->fd);
                    connection->open = 0;
                    open_count -= 1;
                    failures += 1;
                    break;
                }
            }

            if ((events[event_index].events & EPOLLOUT) != 0
                && connection->open
                && !connection->waiting
                && (max_requests == 0 || total_sent < max_requests)) {
                failures += spam_send_request(connection, latencies, &total_sent, max_requests);
            }
        }
    }

    for (i = 0; i < options->connections; ++i) {
        if (connections[i].open) {
            close(connections[i].fd);
        }
    }
    close(epoll_fd);
    free(events);
    free(connections);
    return failures;
}

static stress_options parse_options(int argc, char** argv) {
    stress_options options;
    int i;

    memset(&options, 0, sizeof(options));
    snprintf(options.mode, sizeof(options.mode), "short");
    options.connections = 64;
    options.requests = 1000;
    options.workers = 2;
    options.large_bytes = 256 * 1024;
    options.duration_ms = 250;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            snprintf(options.mode, sizeof(options.mode), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--connections") == 0 && i + 1 < argc) {
            options.connections = (sz)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--requests") == 0 && i + 1 < argc) {
            options.requests = (sz)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            options.workers = (sz)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--large-bytes") == 0 && i + 1 < argc) {
            options.large_bytes = (sz)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--ms") == 0 && i + 1 < argc) {
            options.duration_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: server_stress [--mode idle|short|mixed|slowloris|backpressure|spam] [--connections N] [--requests N] [--workers N] [--large-bytes N] [--ms N]\n");
            exit(0);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            exit(2);
        }
    }
    return options;
}

int main(int argc, char** argv) {
    stress_options options = parse_options(argc, argv);
    sw_server_config config = sw_server_config_default();
    stress_state state;
    sw_server* server;
    latency_list latencies = {0};
    double start_ms;
    double elapsed_ms;
    long rss_before;
    long rss_after;
    sz failures = 0;
    u16 port;

    config.worker_count = options.workers;
    config.max_connections = options.connections + 64;
    config.listen_backlog = 65535;
    config.event_batch_size = 4096;
    config.initial_read_buffer_bytes = 512;
    config.initial_write_buffer_bytes = 512;
    config.max_read_buffer_bytes = 8192;
    if (strcmp(options.mode, "spam") == 0) {
        config.header_timeout_ms = 180000;
        config.body_timeout_ms = 180000;
        config.idle_timeout_ms = 180000;
    } else {
        config.header_timeout_ms = 100;
        config.body_timeout_ms = 100;
        config.idle_timeout_ms = 1000;
    }
    config.max_write_buffer_bytes = options.large_bytes + 4096;

    state.large_body_len = options.large_bytes;
    state.large_body = (char*)malloc(state.large_body_len);
    assert(state.large_body != NULL);
    memset(state.large_body, 'x', state.large_body_len);

    server = sw_server_create(&config);
    assert(server != NULL);
    assert(sw_server_add_http(server, "http://127.0.0.1:0", stress_handler, &state) == 0);
    port = sw_server_get_listener_port(server, 0);
    assert(port != 0);
    assert(sw_server_start(server) == 0);
    s_thread_sleep_ms(20);

    rss_before = stress_rss_bytes();
    rss_after = rss_before;
    start_ms = stress_now_ms();
    if (strcmp(options.mode, "idle") == 0) {
        failures = run_idle(port, &options, &rss_after);
    } else if (strcmp(options.mode, "short") == 0) {
        failures = run_request_loop(port, "/small", options.requests, &latencies);
        rss_after = stress_rss_bytes();
    } else if (strcmp(options.mode, "mixed") == 0) {
        failures = run_keepalive_mixed(port, &options, &latencies, &rss_after);
    } else if (strcmp(options.mode, "slowloris") == 0) {
        failures = run_slowloris(port, &options, &latencies);
        rss_after = stress_rss_bytes();
    } else if (strcmp(options.mode, "backpressure") == 0) {
        failures = run_request_loop(port, "/large", options.requests, &latencies);
        rss_after = stress_rss_bytes();
    } else if (strcmp(options.mode, "spam") == 0) {
        failures = run_spam(port, &options, &latencies, &rss_after);
    } else {
        fprintf(stderr, "unknown mode: %s\n", options.mode);
        failures = 1;
    }
    elapsed_ms = stress_now_ms() - start_ms;

    print_summary(server, &options, &latencies, elapsed_ms, failures, rss_before, rss_after);

    sw_server_stop(server);
    (void)sw_server_wait(server);
    sw_server_destroy(server);
    free(latencies.values);
    free(state.large_body);
    return failures == 0 ? 0 : 1;
}
