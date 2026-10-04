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

static void hello(ResponseWriter *w, Request *r, void *ctx)
{
    (void)ctx;
    (void)r;
    rw_write_string(w, "hi");
}

int main(void)
{
    Arena *a = arena_new();
    Arena *scratch = arena_new();
    Arena *setup = arena_new();
    ASSERT(a && scratch && setup);

    const char *raw = "GET /hello HTTP/1.1\r\nHost: t\r\n\r\n";
    Request *r = NULL;
    size_t consumed = 0;
    ParseError err = REQ_ERR_NONE;
    ASSERT(request_parse(a, raw, strlen(raw), 1 << 20, 8 << 20,
                         &r, &consumed, &err) == REQ_OK);

    FILE *log = tmpfile();
    ASSERT(log != NULL);
    Middleware mws[] = { logging_middleware(setup, log) };
    Handler h = chain(handler_func(hello, NULL), mws, 1);

    ResponseWriter w = writer_new(a);
    handler_serve(h, &w, r);
    ASSERT(w.status == 200);

    char line[256];
    rewind(log);
    ASSERT(fgets(line, sizeof(line), log) != NULL);
    ASSERT(strstr(line, "GET /hello 200") != NULL);
    fclose(log);

    arena_reset(a);
    arena_reset(scratch);

    /* plain GET passes through with the origin stamped */
    const char *get = "GET /r HTTP/1.1\r\nHost: t\r\nOrigin: https://x.test\r\n\r\n";
    r = NULL;
    ASSERT(request_parse(a, get, strlen(get), 1 << 20, 8 << 20,
                         &r, &consumed, &err) == REQ_OK);
    Middleware cms[] = { cors_middleware(setup, "*") };
    Handler ch = chain(handler_func(hello, NULL), cms, 1);
    ResponseWriter w2 = writer_new(a);
    handler_serve(ch, &w2, r);
    ASSERT(w2.status == 200);
    ASSERT(strcmp(header_get(rw_header(&w2), "Access-Control-Allow-Origin"), "*") == 0);

    /* preflight short-circuits with 204 and never hits the handler */
    const char *pre =
        "OPTIONS /r HTTP/1.1\r\nHost: t\r\nOrigin: https://x.test\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Access-Control-Request-Headers: X-Custom\r\n\r\n";
    arena_reset(a);
    arena_reset(scratch);
    r = NULL;
    ASSERT(request_parse(a, pre, strlen(pre), 1 << 20, 8 << 20,
                         &r, &consumed, &err) == REQ_OK);
    ResponseWriter w3 = writer_new(a);
    handler_serve(ch, &w3, r);
    ASSERT(w3.status == 204);
    ASSERT(w3.written == 0);
    ASSERT(strcmp(header_get(rw_header(&w3), "Access-Control-Allow-Methods"), "POST") == 0);
    ASSERT(strcmp(header_get(rw_header(&w3), "Access-Control-Allow-Headers"), "X-Custom") == 0);

    /* specific origin is echoed with a Vary */
    arena_reset(a);
    Middleware cms2[] = { cors_middleware(setup, "https://x.test") };
    Handler ch2 = chain(handler_func(hello, NULL), cms2, 1);
    r = NULL;
    ASSERT(request_parse(a, get, strlen(get), 1 << 20, 8 << 20,
                         &r, &consumed, &err) == REQ_OK);
    ResponseWriter w4 = writer_new(a);
    handler_serve(ch2, &w4, r);
    ASSERT(strcmp(header_get(rw_header(&w4), "Access-Control-Allow-Origin"),
                  "https://x.test") == 0);
    ASSERT(header_get(rw_header(&w4), "Vary") != NULL);

    arena_free(scratch);
    arena_free(setup);
    arena_free(a);

    if (failures == 0)
        printf("test_middleware: ok\n");
    return failures ? 1 : 0;
}
