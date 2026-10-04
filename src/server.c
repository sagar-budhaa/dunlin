#include <dunlin/dunlin.h>

#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>

typedef struct ServerState {
    SOCKET listen_fd;
    volatile int stopping;
    ConnCfg cfg;
    CRITICAL_SECTION lock;
    HANDLE *threads;
    size_t nthreads;
    size_t capthreads;
} ServerState;

typedef struct ConnArg {
    ServerState *st;
    SOCKET sock;
    char remote[64];
} ConnArg;

static void track_add(ServerState *st, HANDLE h)
{
    EnterCriticalSection(&st->lock);
    if (st->nthreads == st->capthreads) {
        size_t ncap = st->capthreads ? st->capthreads * 2 : 16;
        HANDLE *nh = realloc(st->threads, ncap * sizeof(*nh));
        if (nh) {
            st->threads = nh;
            st->capthreads = ncap;
        }
    }
    if (st->nthreads < st->capthreads)
        st->threads[st->nthreads++] = h;
    else
        CloseHandle(h); /* untracked but running; shutdown won't join it */
    LeaveCriticalSection(&st->lock);
}

static void track_remove(ServerState *st, HANDLE h)
{
    EnterCriticalSection(&st->lock);
    for (size_t i = 0; i < st->nthreads; i++) {
        if (st->threads[i] == h) {
            st->threads[i] = st->threads[--st->nthreads];
            break;
        }
    }
    LeaveCriticalSection(&st->lock);
}

static unsigned __stdcall conn_thread(void *arg)
{
    ConnArg *ca = arg;
    ServerState *st = ca->st;
    HANDLE self;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                    GetCurrentProcess(), &self, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    track_add(st, self);
    conn_run(ca->sock, ca->remote, &st->cfg);
    track_remove(st, self);
    CloseHandle(self);
    free(ca);
    return 0;
}

static int split_addr(const char *addr, char *host, size_t hostcap,
                      char *port, size_t portcap)
{
    const char *colon = strrchr(addr, ':');
    if (!colon)
        return -1;
    if ((size_t)(colon - addr) >= hostcap || strlen(colon + 1) >= portcap)
        return -1;
    memcpy(host, addr, (size_t)(colon - addr));
    host[colon - addr] = '\0';
    strcpy(port, colon + 1);
    if (port[0] == '\0')
        return -1;
    return 0;
}

static SOCKET bind_listen(const char *addr)
{
    char host[256], port[16];
    struct addrinfo hints, *res, *rp;
    SOCKET ls = INVALID_SOCKET;

    if (!addr || split_addr(addr, host, sizeof(host), port, sizeof(port)) != 0)
        return INVALID_SOCKET;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(host[0] ? host : NULL, port, &hints, &res) != 0)
        return INVALID_SOCKET;
    for (rp = res; rp; rp = rp->ai_next) {
        BOOL yes = TRUE;
        DWORD v6only = 0;
        ls = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (ls == INVALID_SOCKET)
            continue;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&yes, sizeof(yes));
        if (rp->ai_family == AF_INET6)
            setsockopt(ls, IPPROTO_IPV6, IPV6_V6ONLY,
                       (const char *)&v6only, sizeof(v6only));
        if (bind(ls, rp->ai_addr, (int)rp->ai_addrlen) == 0 &&
            listen(ls, 128) == 0)
            break;
        closesocket(ls);
        ls = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return ls;
}

static void remote_name(SOCKET s, char *out, size_t n)
{
    struct sockaddr_storage ss;
    int slen = sizeof(ss);
    char host[64] = "?";
    char serv[16] = "?";

    if (getpeername(s, (struct sockaddr *)&ss, &slen) == 0)
        getnameinfo((struct sockaddr *)&ss, slen, host, sizeof(host),
                    serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    snprintf(out, n, "%s:%s", host, serv);
}

int server_listen_and_serve(Server *s)
{
    static int wsa_ok = 0;
    ServerState *st;
    WSADATA wd;

    if (!s || !s->addr)
        return -1;
    if (!wsa_ok) {
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0)
            return -1;
        wsa_ok = 1;
    }
    st = calloc(1, sizeof(*st));
    if (!st)
        return -1;
    st->listen_fd = bind_listen(s->addr);
    if (st->listen_fd == INVALID_SOCKET) {
        free(st);
        return -1;
    }
    st->cfg.handler = s->handler;
    st->cfg.max_header_bytes = s->max_header_bytes ? s->max_header_bytes : (1u << 20);
    st->cfg.max_body_bytes = s->max_body_bytes ? s->max_body_bytes : (8u << 20);
    st->cfg.read_timeout_ms = s->read_timeout_ms ? s->read_timeout_ms : 5000;
    st->cfg.write_timeout_ms = s->write_timeout_ms ? s->write_timeout_ms : 10000;
    st->cfg.idle_timeout_ms = s->idle_timeout_ms ? s->idle_timeout_ms : 120000;
    InitializeCriticalSection(&st->lock);
    s->_impl = st;

    for (;;) {
        SOCKET c;
        ConnArg *ca;
        uintptr_t th;
        if (st->stopping)
            break;
        c = accept(st->listen_fd, NULL, NULL);
        if (c == INVALID_SOCKET) {
            if (st->stopping)
                break;
            continue;
        }
        ca = malloc(sizeof(*ca));
        if (!ca) {
            closesocket(c);
            continue;
        }
        ca->st = st;
        ca->sock = c;
        remote_name(c, ca->remote, sizeof(ca->remote));
        th = _beginthreadex(NULL, 0, conn_thread, ca, 0, NULL);
        if (th == 0) {
            closesocket(c);
            free(ca);
        } else {
            CloseHandle((HANDLE)th);
        }
    }

    closesocket(st->listen_fd);
    DeleteCriticalSection(&st->lock);
    free(st->threads);
    s->_impl = NULL;
    free(st);
    return 0;
}

void server_shutdown(Server *s, int grace_ms)
{
    ServerState *st = s ? s->_impl : NULL;
    int waited = 0;

    if (!st)
        return;
    st->stopping = 1;
    closesocket(st->listen_fd);
    /* Closing the listener breaks accept(); poll for drain. */
    for (;;) {
        int busy;
        EnterCriticalSection(&st->lock);
        busy = st->nthreads > 0;
        LeaveCriticalSection(&st->lock);
        if (!busy || (grace_ms >= 0 && waited >= grace_ms))
            break;
        Sleep(50);
        waited += 50;
    }
}

int dunlin_listen_and_serve(const char *addr)
{
    ServeMux *m = dunlin_default_mux();
    Server s;

    if (!m)
        return -1;
    memset(&s, 0, sizeof(s));
    s.addr = addr;
    s.handler = mux_handler(m);
    return server_listen_and_serve(&s);
}

#else
/* TODO(linux): epoll single-loop backend. Handlers must not block there. */

int server_listen_and_serve(Server *s)
{
    (void)s;
    errno = ENOSYS;
    return -1;
}

void server_shutdown(Server *s, int grace_ms)
{
    (void)s;
    (void)grace_ms;
}

int dunlin_listen_and_serve(const char *addr)
{
    (void)addr;
    errno = ENOSYS;
    return -1;
}
#endif
