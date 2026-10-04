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

    arena_reset(a);

    const char *hdrs = "POST /sub HTTP/1.1\r\nHost:  example.com  \r\ncontent-TYPE: text/plain\r\nX-Multi: a\r\nX-Multi: b\r\nX-Empty:\r\n\r\n";
    r = NULL;
    ASSERT(request_parse(a, hdrs, strlen(hdrs), 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(strcmp(req_host(r), "example.com") == 0);
    ASSERT(strcmp(header_get(req_header(r), "Content-Type"), "text/plain") == 0);
    ASSERT(strcmp(header_get(req_header(r), "x-multi"), "a") == 0);
    ASSERT(strcmp(header_get(req_header(r), "X-Empty"), "") == 0);

    const char *nocolon = "GET / HTTP/1.1\r\nNoColonHere\r\n\r\n";
    ASSERT(request_parse(a, nocolon, strlen(nocolon), 8192, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_BAD_HEADER);

    const char *fold = "GET / HTTP/1.1\r\nHost: a\r\n folded\r\n\r\n";
    ASSERT(request_parse(a, fold, strlen(fold), 8192, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_BAD_HEADER);

    arena_reset(a);

    const char *post = "POST /users HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhello world";
    r = NULL;
    ASSERT(request_parse(a, post, strlen(post), 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(consumed == strlen(post));
    size_t blen = 0;
    const char *body = req_body(r, &blen);
    ASSERT(blen == 11);
    ASSERT(memcmp(body, "hello world", 11) == 0);

    /* body arriving in two feeds: incomplete first, then ok */
    arena_reset(a);
    char split[256];
    size_t hlen = (size_t)(strstr(post, "\r\n\r\n") + 4 - post);
    memcpy(split, post, hlen + 5);
    r = NULL;
    ASSERT(request_parse(a, split, hlen + 5, 8192, 8 << 20, &r, &consumed, &err) == REQ_INCOMPLETE);
    memcpy(split + hlen + 5, post + hlen + 5, strlen(post) - hlen - 5);
    ASSERT(request_parse(a, split, strlen(post), 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(req_body(r, &blen) != NULL && blen == 11);

    /* pipelining: two requests, memmove the remainder */
    arena_reset(a);
    char pipe[256];
    const char *second = "GET /two HTTP/1.1\r\n\r\n";
    memcpy(pipe, post, strlen(post));
    memcpy(pipe + strlen(post), second, strlen(second));
    size_t total = strlen(post) + strlen(second);
    r = NULL;
    ASSERT(request_parse(a, pipe, total, 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(strcmp(req_path(r), "/users") == 0);
    memmove(pipe, pipe + consumed, total - consumed);
    total -= consumed;
    arena_reset(a);
    Request *r2 = NULL;
    ASSERT(request_parse(a, pipe, total, 8192, 8 << 20, &r2, &consumed, &err) == REQ_OK);
    ASSERT(strcmp(req_path(r2), "/two") == 0);
    ASSERT(consumed == total);

    const char *big = "GET / HTTP/1.1\r\nHost: a\r\n\r\n";
    ASSERT(request_parse(a, big, strlen(big), 10, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_HEADERS_TOO_LARGE);

    ASSERT(request_parse(a, "GET / HTTP/1.1\r\n", 16, 10, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_HEADERS_TOO_LARGE);

    const char *fat = "POST / HTTP/1.1\r\nContent-Length: 100\r\n\r\n";
    ASSERT(request_parse(a, fat, strlen(fat), 8192, 10, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_BODY_TOO_LARGE);

    const char *chunked = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    ASSERT(request_parse(a, chunked, strlen(chunked), 8192, 8 << 20, &r, &consumed, &err) == REQ_ERROR);
    ASSERT(err == REQ_ERR_TRANSFER_ENCODING);

    const char *ident = "POST / HTTP/1.1\r\nTransfer-Encoding: identity\r\nContent-Length: 3\r\n\r\nabc";
    ASSERT(request_parse(a, ident, strlen(ident), 8192, 8 << 20, &r, &consumed, &err) == REQ_OK);
    ASSERT(req_body(r, &blen) != NULL && blen == 3);

    arena_free(a);

    if (failures == 0)
        printf("test_parse: ok\n");
    return failures ? 1 : 0;
}
