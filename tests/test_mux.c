#include <dunlin/dunlin.h>

#include "internal.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void mark(ResponseWriter *w, Request *r, void *ctx)
{
    (void)r;
    rw_write_string(w, (const char *)ctx);
}

typedef struct {
    const char *method;
    const char *target;
    const char *want_body;
    int want_status;
} Case;

static char got_body[512];
static int got_status;

static int run_case(ServeMux *m, Arena *a, Arena *scratch,
                    const char *method, const char *target,
                    Request **req_out)
{
    char raw[512];
    ResponseWriter w;
    Request *r = NULL;
    size_t consumed = 0;
    ParseError err = REQ_ERR_NONE;
    const char *out = NULL;
    const char *split;
    size_t len = 0, blen;
    int n = snprintf(raw, sizeof(raw), "%s %s HTTP/1.1\r\nHost: t\r\n\r\n",
                     method, target);
    ParseResult pr;

    if (n < 0 || (size_t)n >= sizeof(raw))
        return -1;
    arena_reset(a);
    arena_reset(scratch);
    pr = request_parse(a, raw, (size_t)n, 1 << 20, 8 << 20,
                       &r, &consumed, &err);
    if (pr != REQ_OK)
        return -1;
    w = writer_new(a);
    handler_serve(mux_handler(m), &w, r);
    if (req_out)
        *req_out = r;
    if (writer_serialize(&w, scratch, &out, &len) != 0)
        return -1;
    split = strstr(out, "\r\n\r\n");
    if (!split)
        return -1;
    split += 4;
    blen = len - (size_t)(split - out);
    if (blen >= sizeof(got_body))
        return -1;
    memcpy(got_body, split, blen);
    got_body[blen] = '\0';
    got_status = w.status;
    return 0;
}

int main(void)
{
    Arena *a = arena_new();
    Arena *scratch = arena_new();
    Request *r = NULL;
    ASSERT(a && scratch);

    ServeMux *m = mux_new();
    mux_handle_func(m, "GET /users/new", mark, "exact-new");
    mux_handle_func(m, "GET /users/{id}", mark, "wild-id");
    mux_handle_func(m, "GET /x", mark, "get-x");
    mux_handle_func(m, "/x", mark, "any-x");
    mux_handle_func(m, "/static/", mark, "static-tree");
    mux_handle_func(m, "/static/a/b", mark, "static-exact");
    mux_handle_func(m, "/files/{p...}", mark, "files-rest");
    mux_handle_func(m, "/a/b", mark, "lit-ab");
    mux_handle_func(m, "/a/{x}", mark, "wild-ax");
    mux_handle_func(m, "/a/", mark, "tree-a");
    mux_handle_func(m, "POST /only-post", mark, "post-only");

    Case cases[] = {
        { "GET", "/users/new", "exact-new", 200 },
        { "GET", "/users/42", "wild-id", 200 },
        { "GET", "/x", "get-x", 200 },
        { "POST", "/x", "any-x", 200 },
        { "POST", "/only-post", "post-only", 200 },
        { "GET", "/only-post", "Method Not Allowed\n", 405 },
        { "GET", "/nope", "Not Found\n", 404 },
        { "GET", "/static/a/b", "static-exact", 200 },
        { "GET", "/static/deep/c", "static-tree", 200 },
        { "GET", "/files/a/b/c", "files-rest", 200 },
        { "GET", "/a/b", "lit-ab", 200 },
        { "GET", "/a/zzz", "wild-ax", 200 },
        { "GET", "/a/b/c", "tree-a", 200 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (run_case(m, a, scratch, cases[i].method, cases[i].target,
                     &r) != 0) {
            printf("FAIL case %zu: %s %s did not serve\n",
                   i, cases[i].method, cases[i].target);
            failures++;
            continue;
        }
        ASSERT(got_status == cases[i].want_status);
        ASSERT(strcmp(got_body, cases[i].want_body) == 0);
    }

    ASSERT(run_case(m, a, scratch, "GET", "/users/42", &r) == 0);
    ASSERT(strcmp(req_path_value(r, "id"), "42") == 0);
    ASSERT(run_case(m, a, scratch, "GET", "/files/a/b/c", &r) == 0);
    ASSERT(strcmp(req_path_value(r, "p"), "a/b/c") == 0);

    /* conflicting shape keeps the first registration */
    mux_handle_func(m, "GET /users/{other}", mark, "wild-other");
    ASSERT(run_case(m, a, scratch, "GET", "/users/7", &r) == 0);
    ASSERT(strcmp(got_body, "wild-id") == 0);

    /* "/" catches everything else */
    ServeMux *m2 = mux_new();
    mux_handle_func(m2, "/", mark, "root");
    ASSERT(run_case(m2, a, scratch, "GET", "/anything/at/all", &r) == 0);
    ASSERT(strcmp(got_body, "root") == 0);

    arena_free(scratch);
    arena_free(a);

    if (failures == 0)
        printf("test_mux: ok\n");
    return failures ? 1 : 0;
}
