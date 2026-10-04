#ifndef DUNLIN_H
#define DUNLIN_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* Arena: request-scoped bump allocator. Every handler allocation comes
   from the request arena and is released in one shot. Handlers never
   call free(). */
typedef struct Arena Arena;

Arena *arena_new(void);
void  *arena_alloc(Arena *a, size_t n); /* NULL on OOM, 8-byte aligned */
void  *arena_calloc(Arena *a, size_t n); /* same, zeroed */
char  *arena_strdup(Arena *a, const char *s);
char  *arena_strndup(Arena *a, const char *s, size_t n);

void arena_reset(Arena *a); /* retains internal storage for reuse */
void arena_free(Arena *a);

/* Header: multi-value map like Go's map[string][]string.
   Lookup is case-insensitive; original case is kept on the wire. */
typedef struct Header Header;

Header *header_new(Arena *a);
int header_set(Header *h, const char *key, const char *value); /* replace, 0 ok / -1 OOM */
int header_add(Header *h, const char *key, const char *value); /* append, 0 ok / -1 OOM */
const char *header_get(const Header *h, const char *key); /* first value or NULL */
size_t header_values(const Header *h, const char *key, const char **out, size_t n);
void header_del(Header *h, const char *key);

/* Request: opaque and read-only. All access goes through accessors. */
typedef struct Request Request;

const char *req_method(Request *r);
const char *req_path(Request *r);
const char *req_query(Request *r);
const char *req_proto(Request *r);
const char *req_host(Request *r);
const char *req_remote_addr(Request *r);
Header *req_header(Request *r);
const char *req_body(Request *r, size_t *len);
const char *req_path_value(Request *r, const char *name);
const char *req_query_value(Request *r, const char *name);
const char *req_form_value(Request *r, const char *name);
Arena *req_scratch(Request *r);

/* Request parser: bounded scan, never mutates buf. Parsed strings are
   arena-allocated and valid until arena_reset/arena_free. */
typedef enum {
    REQ_OK = 0, /* parsed; *consumed is set */
    REQ_INCOMPLETE, /* need more bytes; *consumed untouched */
    REQ_ERROR /* malformed; *err is set */
} ParseResult;

typedef enum {
    REQ_ERR_NONE = 0,
    REQ_ERR_BAD_REQUEST_LINE,
    REQ_ERR_BAD_HEADER,
    REQ_ERR_HEADERS_TOO_LARGE, /* -> 431 */
    REQ_ERR_BODY_TOO_LARGE, /* -> 413 */
    REQ_ERR_TRANSFER_ENCODING, /* chunked rejected in v1 -> 411 */
    REQ_ERR_UNSUPPORTED_VERSION /* non-HTTP/1.x -> 505 */
} ParseError;

/* On REQ_INCOMPLETE, buf is unchanged and the caller appends more bytes
   and calls again. On REQ_OK, *consumed bytes were consumed and the
   caller must memmove the remainder before parsing the next request. */
ParseResult request_parse(Arena *a, const char *buf, size_t len,
                          size_t max_header_bytes, size_t max_body_bytes,
                          Request **out, size_t *consumed, ParseError *err);

/* ResponseWriter: vtable so middleware can substitute a forwarding
   writer. status/written live on the struct for middleware inspection. */
typedef struct ResponseWriter ResponseWriter;

struct ResponseWriter {
    void *ctx;
    int (*write)(void *ctx, const char *data, size_t len); /* bytes or -1 */
    void (*write_header)(void *ctx, int status);
    Header *(*header)(void *ctx);
    int status;
    size_t written;
};

static inline void rw_write_header(ResponseWriter *w, int status)
{
    if (w->status != 0)
        return; /* first wins, like net/http */
    w->status = status;
    w->write_header(w->ctx, status);
}

static inline int rw_write(ResponseWriter *w, const char *data, size_t len)
{
    int n;
    if (w->status == 0)
        rw_write_header(w, 200);
    n = w->write(w->ctx, data, len);
    if (n > 0)
        w->written += (size_t)n;
    return n;
}

static inline Header *rw_header(ResponseWriter *w)
{
    return w->header(w->ctx);
}

static inline int rw_write_string(ResponseWriter *w, const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return rw_write(w, s, n);
}

int rw_printf(ResponseWriter *w, const char *fmt, ...);
int rw_vprintf(ResponseWriter *w, const char *fmt, va_list ap);

/* Handler: (fn, ctx) pair so middleware and closures compose. */
typedef void (*HandlerFunc)(ResponseWriter *w, Request *r, void *ctx);

typedef struct Handler {
    HandlerFunc fn;
    void *ctx;
} Handler;

Handler handler_func(HandlerFunc fn, void *ctx);
void handler_serve(Handler h, ResponseWriter *w, Request *r);

/* ServeMux: Go 1.22 pattern semantics. mux_handle is panic-free:
   conflicts are logged, registration skipped. Mux lives for the
   process lifetime (no free in v1). */
typedef struct ServeMux ServeMux;

ServeMux *mux_new(void);
void mux_handle(ServeMux *m, const char *pattern, Handler h);
void mux_handle_func(ServeMux *m, const char *pattern, HandlerFunc fn, void *ctx);
Handler mux_handler(ServeMux *m);

/* Middleware: Handler -> Handler. chain applies mws[0] outermost.
   Same (fn, ctx) shape as Handler: factories capture config in ctx,
   which allocates from the caller's arena (no GC). */
typedef Handler (*MiddlewareFunc)(Handler next, void *ctx);

typedef struct Middleware {
    MiddlewareFunc fn;
    void *ctx;
} Middleware;

Handler chain(Handler h, const Middleware *mws, size_t n);
Middleware logging_middleware(Arena *a, FILE *out);
Middleware cors_middleware(Arena *a, const char *allow_origin);
Middleware recover_middleware(Arena *a);

/* Server: public fields, zero-initializable. timeouts in ms, 0 means
   default (read 5s, write 10s, idle 120s). sizes 0 mean defaults
   (headers 1MB, body 8MB). */
typedef struct Server {
    const char *addr;
    Handler handler;
    int read_timeout_ms;
    int write_timeout_ms;
    int idle_timeout_ms;
    size_t max_header_bytes;
    size_t max_body_bytes;
    void *_impl;
} Server;

int server_listen_and_serve(Server *s);
void server_shutdown(Server *s, int grace_ms);

/* Package-level helpers over the default mux (setup only, not
   thread-safe by contract). */
void dunlin_handle(const char *pattern, Handler h);
void dunlin_handle_func(const char *pattern, HandlerFunc fn, void *ctx);
ServeMux *dunlin_default_mux(void);
int dunlin_listen_and_serve(const char *addr);

#endif
