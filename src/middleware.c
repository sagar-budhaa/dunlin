#include <dunlin/dunlin.h>

#include "internal.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

Handler handler_func(HandlerFunc fn, void *ctx)
{
    Handler h;
    h.fn = fn;
    h.ctx = ctx;
    return h;
}

void handler_serve(Handler h, ResponseWriter *w, Request *r)
{
    if (h.fn)
        h.fn(w, r, h.ctx);
}

Handler chain(Handler h, const Middleware *mws, size_t n)
{
    if (!mws)
        return h;
    while (n > 0) {
        n--;
        h = mws[n].fn(h, mws[n].ctx);
    }
    return h;
}

typedef struct LogWrap {
    Handler next;
    FILE *out;
} LogWrap;

static void logging_serve(ResponseWriter *w, Request *r, void *ctx)
{
    LogWrap *wrap = ctx;
    struct timespec t0, t1;
    long long ms = -1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    handler_serve(wrap->next, w, r);
    if (clock_gettime(CLOCK_MONOTONIC, &t1) == 0) {
        ms = (long long)(t1.tv_sec - t0.tv_sec) * 1000 +
             (t1.tv_nsec - t0.tv_nsec) / 1000000;
    }
    fprintf(wrap->out, "%s %s %d %lldms\n",
            req_method(r), req_path(r), w->status, ms);
}

static Handler logging_apply(Handler next, void *ctx)
{
    LogWrap *wrap = ctx;
    Handler h;
    wrap->next = next;
    h.fn = logging_serve;
    h.ctx = wrap;
    return h;
}

Middleware logging_middleware(Arena *a, FILE *out)
{
    Middleware m = { NULL, NULL };
    LogWrap *wrap;

    if (!a || !out)
        return m;
    wrap = arena_alloc(a, sizeof(*wrap));
    if (!wrap)
        return m;
    wrap->out = out;
    m.fn = logging_apply;
    m.ctx = wrap;
    return m;
}

typedef struct CorsWrap {
    Handler next;
    char *origin;
} CorsWrap;

static void cors_serve(ResponseWriter *w, Request *r, void *ctx)
{
    CorsWrap *cw = ctx;
    Header *h = rw_header(w);

    header_set(h, "Access-Control-Allow-Origin", cw->origin);
    if (strcmp(cw->origin, "*") != 0)
        header_add(h, "Vary", "Origin");
    if (strcmp(req_method(r), "OPTIONS") == 0 &&
        header_get(req_header(r), "Origin") &&
        header_get(req_header(r), "Access-Control-Request-Method")) {
        const char *hdrs =
            header_get(req_header(r), "Access-Control-Request-Headers");
        header_set(h, "Access-Control-Allow-Methods",
                   header_get(req_header(r), "Access-Control-Request-Method"));
        if (hdrs)
            header_set(h, "Access-Control-Allow-Headers", hdrs);
        rw_write_header(w, 204);
        return;
    }
    handler_serve(cw->next, w, r);
}

static Handler cors_apply(Handler next, void *ctx)
{
    CorsWrap *cw = ctx;
    Handler h;
    cw->next = next;
    h.fn = cors_serve;
    h.ctx = cw;
    return h;
}

Middleware cors_middleware(Arena *a, const char *allow_origin)
{
    Middleware m = { NULL, NULL };
    CorsWrap *cw;

    if (!a || !allow_origin)
        return m;
    cw = arena_alloc(a, sizeof(*cw));
    if (!cw)
        return m;
    cw->origin = arena_strdup(a, allow_origin);
    if (!cw->origin)
        return m;
    m.fn = cors_apply;
    m.ctx = cw;
    return m;
}

typedef struct RecoverWrap {
    Handler next;
} RecoverWrap;

static _Thread_local RecoverSlot *current_slot = NULL;

static void crash_handler(int sig)
{
    RecoverSlot *slot = current_slot;
    if (!slot || slot->failed) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    slot->failed = 1;
    longjmp(slot->jb, 1);
}

static void recover_serve(ResponseWriter *w, Request *r, void *ctx)
{
    RecoverWrap *wrap = ctx;
    RecoverSlot local;
    RecoverSlot *slot = request_recover_slot(r);
    RecoverSlot *prev;

    if (!slot)
        slot = &local;
    slot->failed = 0;
    prev = current_slot;
    current_slot = slot;
    if (setjmp(slot->jb) == 0) {
        handler_serve(wrap->next, w, r);
    } else if (w->status == 0) {
        /* handler died before sending anything; sent headers stay sent */
        rw_write_header(w, 500);
        rw_write_string(w, "Internal Server Error\n");
    }
    current_slot = prev;
}

static Handler recover_apply(Handler next, void *ctx)
{
    RecoverWrap *wrap = ctx;
    Handler h;
    wrap->next = next;
    h.fn = recover_serve;
    h.ctx = wrap;
    return h;
}

Middleware recover_middleware(Arena *a)
{
    static int installed = 0;
    Middleware m = { NULL, NULL };
    RecoverWrap *wrap;

    if (!installed) {
        signal(SIGSEGV, crash_handler);
        signal(SIGABRT, crash_handler);
        installed = 1;
    }
    if (!a)
        return m;
    wrap = arena_alloc(a, sizeof(*wrap));
    if (!wrap)
        return m;
    m.fn = recover_apply;
    m.ctx = wrap;
    return m;
}
