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
    ASSERT(a && scratch);

    const char *raw = "GET /hello HTTP/1.1\r\nHost: t\r\n\r\n";
    Request *r = NULL;
    size_t consumed = 0;
    ParseError err = REQ_ERR_NONE;
    ASSERT(request_parse(a, raw, strlen(raw), 1 << 20, 8 << 20,
                         &r, &consumed, &err) == REQ_OK);

    FILE *log = tmpfile();
    ASSERT(log != NULL);
    Middleware mws[] = { logging_middleware(a, log) };
    Handler h = chain(handler_func(hello, NULL), mws, 1);

    ResponseWriter w = writer_new(a);
    handler_serve(h, &w, r);
    ASSERT(w.status == 200);

    char line[256];
    rewind(log);
    ASSERT(fgets(line, sizeof(line), log) != NULL);
    ASSERT(strstr(line, "GET /hello 200") != NULL);
    fclose(log);

    arena_free(scratch);
    arena_free(a);

    if (failures == 0)
        printf("test_middleware: ok\n");
    return failures ? 1 : 0;
}
