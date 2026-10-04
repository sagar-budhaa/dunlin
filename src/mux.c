#include <dunlin/dunlin.h>

#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Seg {
    const char *text; /* literal bytes or param name */
    size_t len;
    int wild; /* {name}: one segment */
    int rest; /* {name...}: remainder, last only */
} Seg;

typedef struct Route {
    char *method; /* NULL = method-less */
    Seg *segs;
    size_t nsegs;
    int subtree; /* pattern ends in '/' */
    Handler handler;
    struct Route *next;
} Route;

struct ServeMux {
    Arena *arena;
    Route *routes;
};

ServeMux *mux_new(void)
{
    ServeMux *m = malloc(sizeof(*m));
    if (!m)
        return NULL;
    m->arena = arena_new();
    if (!m->arena) {
        free(m);
        return NULL;
    }
    m->routes = NULL;
    return m;
}

static void mux_log(const char *what, const char *pattern)
{
    /* panic-free contract: conflicts are logged, registration skipped. */
    fprintf(stderr, "dunlin: %s pattern %s\n", what, pattern);
}

/* Split path (leading '/' guaranteed) into segments. Returns 0 ok,
   -1 bad pattern. Trailing '/' sets *subtree and is dropped. */
static int parse_segs(Arena *a, const char *path, Seg **out, size_t *n,
                      int *subtree)
{
    size_t count = 0;
    const char *p = path + 1;
    Seg *segs;
    size_t i;

    *subtree = 0;
    if (*p == '\0') {
        *subtree = 1; /* "/" matches the whole tree */
        *out = NULL;
        *n = 0;
        return 0;
    }
    for (const char *c = p; ; c++) {
        if (*c == '/' || *c == '\0') {
            if (c == p) {
                if (*c == '\0')
                    *subtree = 1;
                else
                    return -1; /* empty middle segment */
            } else {
                count++;
            }
            if (*c == '\0')
                break;
            p = c + 1;
        }
    }
    segs = arena_calloc(a, sizeof(*segs) * (count ? count : 1));
    if (!segs)
        return -1;
    p = path + 1;
    i = 0;
    for (const char *c = p; ; c++) {
        if (*c == '/' || *c == '\0') {
            size_t len = (size_t)(c - p);
            if (len > 0) {
                segs[i].text = p;
                segs[i].len = len;
                if (p[0] == '{') {
                    if (len < 3 || p[len - 1] != '}')
                        return -1;
                    size_t inner = len - 2;
                    const char *name = p + 1;
                    segs[i].wild = 1;
                    if (inner > 3 && memcmp(name + inner - 3, "...", 3) == 0) {
                        inner -= 3;
                        segs[i].rest = 1;
                    }
                    if (inner == 0)
                        return -1;
                    for (size_t k = 0; k < inner; k++) {
                        if (name[k] == '/')
                            return -1;
                    }
                    segs[i].text = arena_strndup(a, name, inner);
                    segs[i].len = inner;
                    if (!segs[i].text)
                        return -1;
                    if (segs[i].rest && *c != '\0')
                        return -1; /* rest must be last */
                }
                i++;
            }
            if (*c == '\0')
                break;
            p = c + 1;
        }
    }
    *out = segs;
    *n = count;
    return 0;
}

void mux_handle(ServeMux *m, const char *pattern, Handler h)
{
    const char *path;
    const char *method = NULL;
    size_t method_len = 0;
    Route *r, *tail;
    char *mmethod;
    Seg *segs = NULL;
    size_t nsegs = 0;
    int subtree = 0;

    if (!m || !pattern || !h.fn)
        return;

    path = strchr(pattern, ' ');
    if (path) {
        method = pattern;
        method_len = (size_t)(path - pattern);
        path++;
    } else {
        path = pattern;
    }
    if (*path != '/') {
        mux_log("bad", pattern);
        return;
    }
    if (method && method_len == 0) {
        mux_log("bad", pattern);
        return;
    }
    for (const char *c = path; *c; c++) {
        if (*c == ' ' || *c == '\t') {
            mux_log("bad", pattern);
            return;
        }
    }
    if (parse_segs(m->arena, path, &segs, &nsegs, &subtree) != 0) {
        mux_log("bad", pattern);
        return;
    }

    for (r = m->routes; r; r = r->next) {
        int same_method = (method == NULL && r->method == NULL) ||
                          (method != NULL && r->method != NULL &&
                           strlen(r->method) == method_len &&
                           memcmp(r->method, method, method_len) == 0);
        size_t i;
        if (!same_method || r->nsegs != nsegs || r->subtree != subtree)
            continue;
        for (i = 0; i < nsegs; i++) {
            if (r->segs[i].wild != segs[i].wild ||
                r->segs[i].rest != segs[i].rest)
                break;
            if (!r->segs[i].wild &&
                (r->segs[i].len != segs[i].len ||
                 memcmp(r->segs[i].text, segs[i].text, segs[i].len) != 0))
                break;
        }
        if (i == nsegs) {
            mux_log("conflicting", pattern);
            return;
        }
    }

    if (method) {
        mmethod = arena_strndup(m->arena, method, method_len);
        if (!mmethod)
            return;
    } else {
        mmethod = NULL;
    }
    r = arena_alloc(m->arena, sizeof(*r));
    if (!r) {
        mux_log("out of memory for", pattern);
        return;
    }
    r->method = mmethod;
    r->segs = segs;
    r->nsegs = nsegs;
    r->subtree = subtree;
    r->handler = h;
    r->next = NULL;
    if (!m->routes) {
        m->routes = r;
    } else {
        tail = m->routes;
        while (tail->next)
            tail = tail->next;
        tail->next = r;
    }
}

void mux_handle_func(ServeMux *m, const char *pattern, HandlerFunc fn, void *ctx)
{
    Handler h = handler_func(fn, ctx);
    mux_handle(m, pattern, h);
}

/* Segment walker over a request path. *pp enters at a segment start
   (or "") and leaves past the trailing slash. */
static int next_path_seg(const char **pp, const char **s, size_t *slen)
{
    const char *start = *pp;
    const char *c = start;

    if (*c == '\0')
        return 0;
    while (*c && *c != '/')
        c++;
    *s = start;
    *slen = (size_t)(c - start);
    *pp = (*c == '/') ? c + 1 : c;
    return 1;
}

static int route_match(const Route *r, const char *path)
{
    const char *p = path + 1; /* skip leading '/' */
    size_t i;

    for (i = 0; i < r->nsegs; i++) {
        const char *s;
        size_t slen;
        if (r->segs[i].rest)
            return 1;
        if (!next_path_seg(&p, &s, &slen))
            return 0;
        if (r->segs[i].wild) {
            if (slen == 0)
                return 0;
        } else if (slen != r->segs[i].len ||
                   memcmp(s, r->segs[i].text, slen) != 0) {
            return 0;
        }
    }
    if (*p == '\0')
        return 1;
    return r->subtree;
}

/* Higher wins: method-specific, then more literals, then wildcards
   over subtree catch-alls, then exact over subtree. First
   registered wins ties. */
static int route_rank(const Route *r)
{
    int literals = 0, wilds = 0;
    size_t i;

    for (i = 0; i < r->nsegs; i++) {
        if (r->segs[i].wild || r->segs[i].rest)
            wilds++;
        else
            literals++;
    }
    return (r->method ? 100000 : 0) + literals * 100 + wilds * 10 +
           (r->subtree ? 0 : 50);
}

static void route_capture(const Route *r, Request *req, const char *path)
{
    const char *p = path + 1;
    size_t i;

    for (i = 0; i < r->nsegs; i++) {
        const char *s;
        size_t slen;
        if (r->segs[i].rest) {
            request_set_path_param(req, r->segs[i].text, p, strlen(p));
            return;
        }
        next_path_seg(&p, &s, &slen);
        if (r->segs[i].wild)
            request_set_path_param(req, r->segs[i].text, s, slen);
    }
}

static void mux_serve(ResponseWriter *w, Request *req, void *ctx)
{
    ServeMux *m = ctx;
    const char *path = req_path(req);
    const char *method = req_method(req);
    const Route *best = NULL;
    int best_rank = -1;
    int path_hit = 0;

    for (const Route *r = m->routes; r; r = r->next) {
        int rank;
        if (!route_match(r, path))
            continue;
        path_hit = 1;
        if (r->method && strcmp(r->method, method) != 0)
            continue;
        rank = route_rank(r);
        if (rank > best_rank) {
            best = r;
            best_rank = rank;
        }
    }
    if (!best) {
        if (path_hit) {
            rw_write_header(w, 405);
            rw_write_string(w, "Method Not Allowed\n");
        } else {
            rw_write_header(w, 404);
            rw_write_string(w, "Not Found\n");
        }
        return;
    }
    route_capture(best, req, path);
    handler_serve(best->handler, w, req);
}

Handler mux_handler(ServeMux *m)
{
    Handler h;
    h.fn = mux_serve;
    h.ctx = m;
    return h;
}

/* The only global mutable state in the library. Setup-time use
   only; registering while serving is a data race by contract. */
static ServeMux *default_mux = NULL;

ServeMux *dunlin_default_mux(void)
{
    if (!default_mux)
        default_mux = mux_new();
    return default_mux;
}

void dunlin_handle(const char *pattern, Handler h)
{
    ServeMux *m = dunlin_default_mux();
    if (m)
        mux_handle(m, pattern, h);
}

void dunlin_handle_func(const char *pattern, HandlerFunc fn, void *ctx)
{
    dunlin_handle(pattern, handler_func(fn, ctx));
}
