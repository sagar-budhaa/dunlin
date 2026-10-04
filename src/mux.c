#include <dunlin/dunlin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Route {
    char *method; /* NULL = method-less */
    char *path;
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

void mux_handle(ServeMux *m, const char *pattern, Handler h)
{
    const char *path;
    const char *method = NULL;
    size_t method_len = 0, path_len;
    Route *r, *tail;
    char *mpath, *mmethod;

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

    for (r = m->routes; r; r = r->next) {
        int same_method = (method == NULL && r->method == NULL) ||
                          (method != NULL && r->method != NULL &&
                           strlen(r->method) == method_len &&
                           memcmp(r->method, method, method_len) == 0);
        if (same_method && strcmp(r->path, path) == 0) {
            mux_log("conflicting", pattern);
            return;
        }
    }

    path_len = strlen(path);
    mpath = arena_strndup(m->arena, path, path_len);
    if (method) {
        mmethod = arena_strndup(m->arena, method, method_len);
        if (!mmethod)
            return;
    } else {
        mmethod = NULL;
    }
    r = arena_alloc(m->arena, sizeof(*r));
    if (!mpath || !r) {
        mux_log("out of memory for", pattern);
        return;
    }
    r->method = mmethod;
    r->path = mpath;
    r->subtree = path_len > 0 && mpath[path_len - 1] == '/';
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

static int path_match(const Route *r, const char *path)
{
    size_t n = strlen(r->path);
    if (!r->subtree)
        return strcmp(r->path, path) == 0;
    return strncmp(r->path, path, n) == 0;
}

/* Higher number wins. Method-specific beats method-less, exact
   beats subtree. First registered wins within a class. */
static int route_rank(const Route *r)
{
    int rank = 0;
    if (r->method)
        rank += 2;
    if (!r->subtree)
        rank += 1;
    return rank;
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
        if (!path_match(r, path))
            continue;
        path_hit = 1;
        if (r->method && strcmp(r->method, method) != 0)
            continue;
        int rank = route_rank(r);
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
    handler_serve(best->handler, w, req);
}

Handler mux_handler(ServeMux *m)
{
    Handler h;
    h.fn = mux_serve;
    h.ctx = m;
    return h;
}
