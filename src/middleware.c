#include <dunlin/dunlin.h>

#include <stddef.h>
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
