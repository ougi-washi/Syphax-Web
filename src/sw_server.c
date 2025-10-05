#include "sw_server.h"
#include <sys/epoll.h>
#include <sys/sendfile.h>

static i32 sw_set_nonblocking(i32 sock) {
    i32 flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

static i32 sw_create_listener(const c8 *addr, i32 port) {
    i32 sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) return -1;
    
    i32 opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = addr ? inet_addr(addr) : INADDR_ANY;
    
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        close(sock);
        return -1;
    }
    
    if (listen(sock, 128) == -1) {
        close(sock);
        return -1;
    }
    
    sw_set_nonblocking(sock);
    return sock;
}

i32 sw_mgr_init(sw_mgr_t *mgr) {
    memset(mgr, 0, sizeof(*mgr));
    mgr->epoll_fd = epoll_create1(0);
    if (mgr->epoll_fd == -1) return -1;
    return 0;
}

// TODO: check https support
i32 sw_http_listen(sw_mgr_t *mgr, const c8 *url) {
    c8 host[256];
    i32 port;
    
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;
    }
    
    const c8 *colon = strchr(url, ':');
    if (colon) {
        sz host_len = colon - url;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        memcpy(host, url, host_len);
        host[host_len] = '\0';
        port = atoi(colon + 1);
    } else {
        strcpy(host, url);
        port = 80;
    }
    
    i32 sock = sw_create_listener(*host ? host : NULL, port);
    if (sock == -1) return -1;
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if (epoll_ctl(mgr->epoll_fd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        close(sock);
        return -1;
    }
    
    return 0;
}

static sw_connection_t *sw_accept_connection(sw_mgr_t *mgr, i32 sock) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    i32 conn_sock = accept(sock, (struct sockaddr *)&addr, &len);
    
    if (conn_sock == -1) return NULL;
    
    sw_connection_t *conn = calloc(1, sizeof(*conn));
    if (!conn) {
        close(conn_sock);
        return NULL;
    }
    
    conn->sock = conn_sock;
    conn->addr = addr;
    conn->last_activity = time(NULL);
    sw_set_nonblocking(conn_sock);
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = conn;
    if (epoll_ctl(mgr->epoll_fd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
        close(conn_sock);
        free(conn);
        return NULL;
    }
    
    mgr->conns[mgr->num_conns++] = conn;
    return conn;
}

static void sw_close_connection(sw_mgr_t *mgr, sw_connection_t *conn) {
    for (i32 i = 0; i < mgr->num_conns; i++) {
        if (mgr->conns[i] == conn) {
            mgr->conns[i] = mgr->conns[--mgr->num_conns];
            break;
        }
    }
    
    epoll_ctl(mgr->epoll_fd, EPOLL_CTL_DEL, conn->sock, NULL);
    close(conn->sock);
    if (conn->request) {
        free(conn->request->body);
        free(conn->request);
    }
    free(conn);
}

static i32 sw_read_connection(sw_connection_t *conn) {
    c8 buf[4096];
    sz n = read(conn->sock, buf, sizeof(buf));
    
    if (n <= 0) return n;
    
    if (conn->read_len + n > SW_MAX_READ_SIZE) {
        return -1;
    }
    
    memcpy(conn->read_buf + conn->read_len, buf, n);
    conn->read_len += n;
    conn->last_activity = time(NULL);
    
    return n;
}

static i32 sw_write_connection(sw_connection_t *conn) {
    if (conn->write_len == 0) return 0;
    
    sz n = write(conn->sock, conn->write_buf, conn->write_len);
    if (n <= 0) return n;
    
    memmove(conn->write_buf, conn->write_buf + n, conn->write_len - n);
    conn->write_len -= n;
    conn->last_activity = time(NULL);
    
    return n;
}

i32 sw_mgr_poll(sw_mgr_t *mgr, i32 timeout_ms) {
    struct epoll_event events[64];
    i32 nfds = epoll_wait(mgr->epoll_fd, events, 64, timeout_ms);
    
    for (i32 i = 0; i < nfds; i++) {
        if (events[i].data.fd >= 0) {
            i32 sock = events[i].data.fd;
            sw_accept_connection(mgr, sock);
        } else {
            sw_connection_t *conn = events[i].data.ptr;
            
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                sw_close_connection(mgr, conn);
                continue;
            }
            
            if (events[i].events & EPOLLIN) {
                i32 n = sw_read_connection(conn);
                if (n <= 0) {
                    sw_close_connection(mgr, conn);
                    continue;
                }
                
                sw_http_parse_request(conn);
                
                // If we have a complete request and a handler is set, call it
                if (conn->request && mgr->http_handler) {
                    mgr->http_handler(conn, conn->request);
                }
            }
            
            if (events[i].events & EPOLLOUT && conn->write_len > 0) {
                i32 n = sw_write_connection(conn);
                if (n < 0) {
                    sw_close_connection(mgr, conn);
                    continue;
                }
                
                if (conn->is_draining && conn->write_len == 0) {
                    sw_close_connection(mgr, conn);
                }
            }
        }
    }
    
    time_t now = time(NULL);
    for (i32 i = 0; i < mgr->num_conns; i++) {
        if (now - mgr->conns[i]->last_activity > 30) {
            sw_close_connection(mgr, mgr->conns[i]);
            i--;
        }
    }
    
    return nfds;
}

void sw_mgr_set_http_handler(sw_mgr_t *mgr, void (*handler)(sw_connection_t *, sw_http_message_t *)) {
    mgr->http_handler = handler;
}

void sw_mgr_free(sw_mgr_t *mgr) {
    for (i32 i = 0; i < mgr->num_conns; i++) {
        sw_close_connection(mgr, mgr->conns[i]);
    }
    close(mgr->epoll_fd);
}

void sw_http_parse_request(sw_connection_t *c) {
    if (c->request) return;
    
    c8 *buf = c->read_buf;
    
    c8 *line_end = strstr(buf, "\r\n");
    if (!line_end) return;
    
    *line_end = '\0';
    
    c8 method[16], uri[SW_MAX_URL_SIZE], proto[16];
    if (sscanf(buf, "%15s %2047s %15s", method, uri, proto) != 3) {
        *line_end = '\r';
        return;
    }
    
    sw_http_message_t *hm = calloc(1, sizeof(*hm));
    if (!hm) return;
    
    hm->method = strdup(method);
    hm->uri = strdup(uri);
    hm->proto = strdup(proto);
    
    c8 *header_start = line_end + 2;
    c8 *body_start = strstr(header_start, "\r\n\r\n");
    
    if (body_start) {
        *body_start = '\0';
        body_start += 4;
        hm->body = strdup(body_start);
        hm->body_len = strlen(body_start);
    }
    
    c8 *line = header_start;
    while ((line_end = strstr(line, "\r\n")) && hm->num_headers < SW_MAX_HTTP_HEADERS) {
        *line_end = '\0';
        
        c8 *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            
            c8 *value = colon + 1;
            while (*value && isspace(*value)) value++;
            
            hm->headers[hm->num_headers].name = strdup(line);
            hm->headers[hm->num_headers].value = strdup(value);
            hm->num_headers++;
            
            if (strcasecmp(line, "Content-Length") == 0) {
                hm->content_length = atoi(value);
            } else if (strcasecmp(line, "Transfer-Encoding") == 0) {
                hm->is_chunked = strstr(value, "chunked") != NULL;
            }
        }
        
        line = line_end + 2;
    }
    
    c->request = hm;
    
    memmove(c->read_buf, c->read_buf + c->read_len - (body_start ? strlen(body_start) : 0), 
            body_start ? strlen(body_start) : 0);
    c->read_len = body_start ? strlen(body_start) : 0;
}

void sw_http_handle_request(sw_connection_t *c) {
    if (!c->request) return;
    
    // For now, just send a basic response
    sw_http_reply(c, 200, "Content-Type: text/plain\r\n", "Hello from Syphax Web!");
}

i32 sw_http_reply(sw_connection_t *c, i32 status_code, const c8 *headers, const c8 *fmt, ...) {
    c8 status_line[256];
    const c8 *status_text = "OK";
    
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 201: status_text = "Created"; break;
        case 204: status_text = "No Content"; break;
        case 301: status_text = "Moved Permanently"; break;
        case 302: status_text = "Found"; break;
        case 400: status_text = "Bad Request"; break;
        case 401: status_text = "Unauthorized"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 503: status_text = "Service Unavailable"; break;
        default: status_text = "Unknown"; break;
    }
    
    snprintf(status_line, sizeof(status_line), "HTTP/1.1 %d %s\r\n", status_code, status_text);
    
    c8 body[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    
    c8 response[8192];
    i32 len = snprintf(response, sizeof(response), 
                      "%s"
                      "%s"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n"
                      "\r\n"
                      "%s",
                      status_line, headers ? headers : "", strlen(body), body);
    
    if (c->write_len + len > SW_MAX_WRITE_SIZE) {
        return -1;
    }
    
    memcpy(c->write_buf + c->write_len, response, len);
    c->write_len += len;
    c->is_draining = 1;
    
    return 0;
}

i32 sw_printf(sw_connection_t *c, const c8 *fmt, ...) {
    c8 buf[4096];
    va_list ap;
    va_start(ap, fmt);
    i32 len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    if (c->write_len + len > SW_MAX_WRITE_SIZE) {
        return -1;
    }
    
    memcpy(c->write_buf + c->write_len, buf, len);
    c->write_len += len;
    
    return len;
}

i32 sw_http_get_var(sw_http_message_t *hm, const c8 *name, c8 *buf, sz buf_len) {
    if (!hm->body) return -1;
    
    c8 *data = hm->body;
    sz data_len = hm->body_len;
    
    c8 search[256];
    snprintf(search, sizeof(search), "%s=", name);
    
    c8 *p = strstr(data, search);
    if (!p) return -1;
    
    p += strlen(search);
    
    c8 *end = strchr(p, '&');
    if (!end) end = data + data_len;
    
    sz val_len = end - p;
    if (val_len >= buf_len) val_len = buf_len - 1;
    
    memcpy(buf, p, val_len);
    buf[val_len] = '\0';
    
    for (sz i = 0; i < val_len; i++) {
        if (buf[i] == '+') buf[i] = ' ';
    }
    
    return (int)val_len;
}

i32 sw_match(const c8 *pattern, const c8 *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            
            while (*str) {
                if (sw_match(pattern, str)) return 1;
                str++;
            }
            return 0;
        } else if (*pattern == '?') {
            pattern++;
            str++;
        } else if (*pattern == *str) {
            pattern++;
            str++;
        } else {
            return 0;
        }
    }
    
    while (*pattern == '*') pattern++;
    
    return !*pattern && !*str;
}

i32 sw_http_serve_file(sw_connection_t *c, const c8 *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return sw_http_reply(c, 404, "Content-Type: text/plain\r\n", "File not found");
    }
    
    if (S_ISDIR(st.st_mode)) {
        return sw_http_reply(c, 403, "Content-Type: text/plain\r\n", "Directory listing not allowed");
    }
    
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return sw_http_reply(c, 403, "Content-Type: text/plain\r\n", "Cannot open file");
    }
    
    c8 *content_type = "application/octet-stream";
    const c8 *ext = strrchr(path, '.');
    if (ext) {
        if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) {
            content_type = "text/html";
        } else if (strcasecmp(ext, ".css") == 0) {
            content_type = "text/css";
        } else if (strcasecmp(ext, ".js") == 0) {
            content_type = "application/javascript";
        } else if (strcasecmp(ext, ".json") == 0) {
            content_type = "application/json";
        } else if (strcasecmp(ext, ".png") == 0) {
            content_type = "image/png";
        } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
            content_type = "image/jpeg";
        } else if (strcasecmp(ext, ".gif") == 0) {
            content_type = "image/gif";
        } else if (strcasecmp(ext, ".mp4") == 0) {
            content_type = "video/mp4";
        } else if (strcasecmp(ext, ".webm") == 0) {
            content_type = "video/webm";
        } else if (strcasecmp(ext, ".ogv") == 0) {
            content_type = "video/ogg";
        } else if (strcasecmp(ext, ".mp3") == 0) {
            content_type = "audio/mpeg";
        } else if (strcasecmp(ext, ".txt") == 0) {
            content_type = "text/plain";
        }
    }
    
    c8 headers[512];
    snprintf(headers, sizeof(headers), 
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n",
             content_type, (size_t)st.st_size);
    
    sw_printf(c, "HTTP/1.1 200 OK\r\n%s\r\n", headers);
    
    c8 buf[4096];
    sz total_sent = 0;
    
    while (total_sent < (size_t)st.st_size) {
        sz to_read = sizeof(buf);
        if (to_read > (size_t)st.st_size - total_sent) {
            to_read = (size_t)st.st_size - total_sent;
        }
        
        sz n = fread(buf, 1, to_read, fp);
        if (n == 0) break;
        
        if (c->write_len + n > SW_MAX_WRITE_SIZE) {
            // sw_write_connection(c);
        }
        
        memcpy(c->write_buf + c->write_len, buf, n);
        c->write_len += n;
        total_sent += n;
    }
    
    fclose(fp);
    c->is_draining = 1;
    
    return 0;
}

// TODO: Test this extensively
i32 sw_http_next_multipart(sw_http_message_t *hm, sw_http_multipart_t *mp, sz *offset) {
    if (!hm->body || !offset) return 0;
    
    c8 *boundary = NULL;
    for (i32 i = 0; i < hm->num_headers; i++) {
        if (strcasecmp(hm->headers[i].name, "Content-Type") == 0) {
            c8 *p = strstr(hm->headers[i].value, "boundary=");
            if (p) {
                boundary = p + 9;
                break;
            }
        }
    }
    
    if (!boundary) return 0;
    
    c8 boundary_str[256];
    snprintf(boundary_str, sizeof(boundary_str), "--%s", boundary);
    
    c8 *data = hm->body + *offset;
    // sz remaining = hm->body_len - *offset; // Not used
    
    c8 *part_start = strstr(data, boundary_str);
    if (!part_start) return 0;
    
    part_start += strlen(boundary_str);
    if (*part_start == '-' && *(part_start + 1) == '-') {
        return 0;
    }
    
    part_start += 2;
    
    c8 *headers_end = strstr(part_start, "\r\n\r\n");
    if (!headers_end) return 0;
    
    c8 *body_start = headers_end + 4;
    
    c8 *next_boundary = strstr(body_start, boundary_str);
    if (!next_boundary) return 0;
    
    memset(mp, 0, sizeof(*mp));
    
    c8 *line = part_start;
    while (line < headers_end) {
        c8 *line_end = strstr(line, "\r\n");
        if (!line_end || line_end >= headers_end) break;
        
        *line_end = '\0';
        
        if (strncasecmp(line, "Content-Disposition: form-data", 30) == 0) {
            c8 *name = strstr(line, "name=\"");
            if (name) {
                name += 6;
                c8 *name_end = strchr(name, '"');
                if (name_end) {
                    *name_end = '\0';
                    mp->name = strdup(name);
                    *name_end = '"';
                }
            }
            
            c8 *filename = strstr(line, "filename=\"");
            if (filename) {
                filename += 10;
                c8 *filename_end = strchr(filename, '"');
                if (filename_end) {
                    *filename_end = '\0';
                    mp->filename = strdup(filename);
                    *filename_end = '"';
                }
            }
        } else if (strncasecmp(line, "Content-Type:", 13) == 0) {
            c8 *type = line + 13;
            while (*type && isspace(*type)) type++;
            mp->content_type = strdup(type);
        }
        
        line = line_end + 2;
    }
    
    mp->data = body_start;
    mp->data_len = next_boundary - body_start - 2;
    mp->boundary = strdup(boundary);
    
    *offset = next_boundary - hm->body;
    
    return 1;
}
