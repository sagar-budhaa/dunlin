#include <dunlin/dunlin.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

int main(void)
{
    Arena *a = arena_new();
    ASSERT(a != NULL);

    const char *raw = "GET /hello?name=x HTTP/1.1\r\nHost: example.com\r\n\r\n";
    Request *r = NULL;
    size_t consumed = 0;
    ParseError err = REQ_ERR_NONE;
    ASSERT(request_parse(a, raw, strlen(raw), 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(r != NULL);
    ASSERT(consumed == strlen(raw));
    ASSERT(strcmp(req_method(r), "GET") == 0);
    ASSERT(strcmp(req_path(r), "/hello") == 0);
    ASSERT(strcmp(req_query(r), "name=x") == 0);
    ASSERT(strcmp(req_proto(r), "HTTP/1.1") == 0);
    ASSERT(req_header(r) != NULL);

    arena_reset(a);

    const char *raw2 = "GET / HTTP/1.0\r\n\r\n";
    r = NULL;
    ASSERT(request_parse(a, raw2, strlen(raw2), 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(strcmp(req_path(r), "/") == 0);
    ASSERT(strcmp(req_query(r), "") == 0);

    arena_reset(a);

    const char *part = "GET / HTTP/1.1\r\nHost: a";
    r = NULL;
    ASSERT(request_parse(a, part, strlen(part), 8192, 8 << 20, &r, &consumed, &err) == REQ_INCOMPLETE);
    ASSERT(r == NULL);

    const char *bad = "HELLO\r\n\r\n";
    ASSERT(request_parse(a, bad, strlen(bad), 8192, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_BAD_REQUEST_LINE);

    const char *h2 = "GET / HTTP/2.0\r\n\r\n";
    ASSERT(request_parse(a, h2, strlen(h2), 8192, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_UNSUPPORTED_VERSION);

    arena_free(a);

    if (failures == 0)
        printf("test_parse: ok\n");
    return failures ? 1 : 0;
}
