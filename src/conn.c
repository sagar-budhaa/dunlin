#include <dunlin/dunlin.h>

#include "internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* winsock comes via internal.h */
#else
/* TODO(linux): epoll + non-blocking backend. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#define RECV_BUF_SIZE (64u * 1024u)

static int send_all(sock_t s, const char *buf, size_t len)
{
    while (len > 0) {
        int n = send(s, buf, (int)(len > INT_MAX ? INT_MAX : len), 0);
        if (n <= 0)
            return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int error_status(ParseError err)
{
    switch (err) {
    case REQ_ERR_HEADERS_TOO_LARGE: return 431;
    case REQ_ERR_BODY_TOO_LARGE: return 413;
    case REQ_ERR_TRANSFER_ENCODING: return 411;
    case REQ_ERR_UNSUPPORTED_VERSION: return 505;
    default: return 400;
    }
}

/* Best-effort error reply; connection always closes after. */
static void send_error(sock_t s, Arena *arena, Arena *scratch, int status)
{
    ResponseWriter w = writer_new(arena);
    const char *out = NULL;
    size_t len = 0;

    rw_write_header(&w, status);
    rw_write_string(&w, "Bad Request\n");
    if (writer_serialize(&w, scratch, &out, &len) == 0)
        send_all(s, out, len);
}

static int keep_alive(Request *r)
{
    const char *proto = req_proto(r);
    const char *conn = header_get(req_header(r), "Connection");
    int is11 = proto && strcmp(proto, "HTTP/1.1") == 0;
    if (conn && (strcmp(conn, "close") == 0 || strcmp(conn, "Close") == 0))
        return 0;
    if (is11)
        return 1;
    return conn != NULL; /* HTTP/1.0 + "Connection: keep-alive" */
}

static void set_rcvtimeo(sock_t s, int ms)
{
#ifdef _WIN32
    DWORD tv = (DWORD)(ms < 0 ? 0 : ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

static void set_sndtimeo(sock_t s, int ms)
{
#ifdef _WIN32
    DWORD tv = (DWORD)(ms < 0 ? 0 : ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* Owns the socket for the life of the call. Thread-per-connection on
   Windows (handlers may block); single-loop epoll on Linux (TODO). */
void conn_run(sock_t s, const char *remote, const ConnCfg *cfg)
{
    Arena *arena = arena_new();
    Arena *scratch = arena_new();
    char *buf = NULL;
    size_t have = 0, cap = RECV_BUF_SIZE;
    RecoverSlot slot;

    if (!arena || !scratch)
        goto done;
    buf = malloc(cap);
    if (!buf)
        goto done;

    set_sndtimeo(s, cfg->write_timeout_ms);
    for (;;) {
        Request *r = NULL;
        size_t consumed = 0;
        ParseError err = REQ_ERR_NONE;
        ParseResult pr;
        int first = (have == 0);

        set_rcvtimeo(s, first ? cfg->read_timeout_ms : cfg->idle_timeout_ms);
        for (;;) {
            if (have >= cap) {
                /* buffered more than a full window without a parse;
                   let the parser call it too large. */
                err = REQ_ERR_HEADERS_TOO_LARGE;
                pr = REQ_ERROR;
                break;
            }
            pr = request_parse(arena, buf, have, cfg->max_header_bytes,
                               cfg->max_body_bytes, &r, &consumed, &err);
            if (pr != REQ_INCOMPLETE)
                break;
            {
                int n = recv(s, buf + have, (int)(cap - have), 0);
                if (n <= 0)
                    goto done;
                have += (size_t)n;
            }
        }
        if (pr == REQ_ERROR) {
            send_error(s, arena, scratch, error_status(err));
            goto done;
        }

        request_set_remote_addr(r, remote);
        request_set_recover_slot(r, &slot);

        {
            ResponseWriter w = writer_new(arena);
            const char *out = NULL;
            size_t len = 0;
            handler_serve(cfg->handler, &w, r);
            if (writer_serialize(&w, scratch, &out, &len) != 0)
                goto done;
            if (send_all(s, out, len) != 0)
                goto done;
            if (!keep_alive(r))
                goto done;
        }

        arena_reset(arena);
        arena_reset(scratch);
        memmove(buf, buf + consumed, have - consumed);
        have -= consumed;
    }

done:
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    free(buf);
    arena_free(scratch);
    arena_free(arena);
}
