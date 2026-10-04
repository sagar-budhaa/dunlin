#include <dunlin/dunlin.h>

#include <stddef.h>

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
    while (n > 0)
        h = mws[--n](h);
    return h;
}
