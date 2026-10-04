#include <dunlin/dunlin.h>

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct BodyChunk {
    struct BodyChunk *next;
    size_t len;
    char data[];
} BodyChunk;

typedef struct WriterImpl {
    Arena *arena;
    Header *headers;
    Header *snap; /* frozen at first write_header */
    int status; /* 0 until first write_header */
    BodyChunk *head;
    BodyChunk *tail;
    size_t body_len;
} WriterImpl;

static void impl_write_header(void *ctx, int status);
static Header *impl_header(void *ctx);

static int impl_write(void *ctx, const char *data, size_t len)
{
    WriterImpl *im = ctx;
    BodyChunk *c;

    if (!data && len > 0)
        return -1;
    if (im->status == 0)
        impl_write_header(ctx, 200);
    if (len == 0)
        return 0;
    c = arena_alloc(im->arena, sizeof(*c) + len);
    if (!c)
        return -1;
    memcpy(c->data, data, len);
    c->len = len;
    c->next = NULL;
    if (im->tail)
        im->tail->next = c;
    else
        im->head = c;
    im->tail = c;
    im->body_len += len;
    return (int)len;
}

typedef struct SnapCtx {
    Arena *arena;
    Header *dst;
    int ok;
} SnapCtx;

static void snap_one(const char *key, const char *value, void *ctx)
{
    SnapCtx *s = ctx;
    if (s->ok && header_add(s->dst, key, value) != 0)
        s->ok = 0;
}

static void impl_write_header(void *ctx, int status)
{
    WriterImpl *im = ctx;
    SnapCtx s;

    if (im->status != 0)
        return;
    im->status = status;
    im->snap = header_new(im->arena);
    if (!im->snap)
        return;
    s.arena = im->arena;
    s.dst = im->snap;
    s.ok = 1;
    header_each(im->headers, snap_one, &s);
}

static Header *impl_header(void *ctx)
{
    return ((WriterImpl *)ctx)->headers;
}

ResponseWriter writer_new(Arena *a)
{
    ResponseWriter w = { 0 };
    WriterImpl *im = arena_calloc(a, sizeof(*im));

    if (!im)
        return w;
    im->arena = a;
    im->headers = header_new(a);
    w.ctx = im;
    w.write = impl_write;
    w.write_header = impl_write_header;
    w.header = impl_header;
    w.status = 0;
    w.written = 0;
    return w;
}

static const char *reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Content Too Large";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 505: return "HTTP Version Not Supported";
    default: return "Status";
    }
}

typedef struct EmitCtx {
    char *p;
} EmitCtx;

static void emit_one(const char *key, const char *value, void *ctx)
{
    EmitCtx *e = ctx;
    size_t k = strlen(key), v = strlen(value);
    memcpy(e->p, key, k);
    e->p += k;
    memcpy(e->p, ": ", 2);
    e->p += 2;
    memcpy(e->p, value, v);
    e->p += v;
    memcpy(e->p, "\r\n", 2);
    e->p += 2;
}

typedef struct CountCtx {
    size_t n;
} CountCtx;

static void count_one(const char *key, const char *value, void *ctx)
{
    CountCtx *c = ctx;
    c->n += strlen(key) + 2 + strlen(value) + 2;
}

int writer_serialize(ResponseWriter *w, Arena *scratch,
                     const char **out, size_t *len)
{
    WriterImpl *im = w->ctx;
    Header *hdrs;
    int status = im->status ? im->status : 200;
    char line[64], clen[32];
    int line_len, clen_len = 0;
    size_t total;
    char *buf, *p;
    BodyChunk *c;
    EmitCtx e;
    CountCtx count = { 0 };

    if (!out || !len)
        return -1;
    if (!im->snap)
        impl_write_header(im, status);
    hdrs = im->snap;
    if (!hdrs)
        return -1;

    line_len = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n",
                        status, reason(status));
    if (line_len < 0 || (size_t)line_len >= sizeof(line))
        return -1;

    header_each(hdrs, count_one, &count);

    if (!header_get(hdrs, "Content-Length")) {
        clen_len = snprintf(clen, sizeof(clen), "Content-Length: %lu\r\n",
                            (unsigned long)im->body_len);
        if (clen_len < 0 || (size_t)clen_len >= sizeof(clen))
            return -1;
    }

    total = (size_t)line_len + count.n + (size_t)clen_len + 2 + im->body_len;
    buf = arena_alloc(scratch, total + 1);
    if (!buf)
        return -1;

    p = buf;
    memcpy(p, line, (size_t)line_len);
    p += line_len;
    e.p = p;
    header_each(hdrs, emit_one, &e);
    p = e.p;
    if (clen_len > 0) {
        memcpy(p, clen, (size_t)clen_len);
        p += clen_len;
    }
    memcpy(p, "\r\n", 2);
    p += 2;
    for (c = im->head; c; c = c->next) {
        memcpy(p, c->data, c->len);
        p += c->len;
    }
    *out = buf;
    *len = total;
    return 0;
}

int rw_vprintf(ResponseWriter *w, const char *fmt, va_list ap)
{
    char stack[1024];
    va_list aq;
    int n, m;
    char *heap = NULL;
    int rc;

    va_copy(aq, ap);
    n = vsnprintf(stack, sizeof(stack), fmt, ap);
    if (n < 0) {
        va_end(aq);
        return -1;
    }
    if ((size_t)n < sizeof(stack)) {
        va_end(aq);
        return rw_write(w, stack, (size_t)n);
    }
    heap = malloc((size_t)n + 1);
    if (!heap) {
        va_end(aq);
        return -1;
    }
    m = vsnprintf(heap, (size_t)n + 1, fmt, aq);
    va_end(aq);
    if (m != n) {
        free(heap);
        return -1;
    }
    rc = rw_write(w, heap, (size_t)n);
    free(heap);
    return rc;
}

int rw_printf(ResponseWriter *w, const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = rw_vprintf(w, fmt, ap);
    va_end(ap);
    return rc;
}
