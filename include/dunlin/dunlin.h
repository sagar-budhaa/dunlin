#ifndef DUNLIN_H
#define DUNLIN_H

#include <stddef.h>

/* Arena: request-scoped bump allocator. Every handler allocation comes
   from the request arena and is released in one shot. Handlers never
   call free(). */
typedef struct Arena Arena;

Arena *arena_new(void);
void  *arena_alloc(Arena *a, size_t n); /* NULL on OOM, 8-byte aligned */
void  *arena_calloc(Arena *a, size_t n); /* same, zeroed */
char  *arena_strdup(Arena *a, const char *s);
char  *arena_strndup(Arena *a, const char *s, size_t n);

void arena_reset(Arena *a); /* retains internal storage for reuse */
void arena_free(Arena *a);

/* Header: multi-value map like Go's map[string][]string.
   Lookup is case-insensitive; original case is kept on the wire. */
typedef struct Header Header;

Header *header_new(Arena *a);
int header_set(Header *h, const char *key, const char *value); /* replace, 0 ok / -1 OOM */
int header_add(Header *h, const char *key, const char *value); /* append, 0 ok / -1 OOM */
const char *header_get(const Header *h, const char *key); /* first value or NULL */
size_t header_values(const Header *h, const char *key, const char **out, size_t n);
void header_del(Header *h, const char *key);

/* Request: opaque and read-only. All access goes through accessors. */
typedef struct Request Request;

const char *req_method(Request *r);
const char *req_path(Request *r);
const char *req_query(Request *r);
const char *req_proto(Request *r);
const char *req_host(Request *r);
const char *req_remote_addr(Request *r);
Header *req_header(Request *r);
const char *req_body(Request *r, size_t *len);
const char *req_path_value(Request *r, const char *name);
const char *req_query_value(Request *r, const char *name);
const char *req_form_value(Request *r, const char *name);
Arena *req_scratch(Request *r);

/* Request parser: bounded scan, never mutates buf. Parsed strings are
   arena-allocated and valid until arena_reset/arena_free. */
typedef enum {
    REQ_OK = 0, /* parsed; *consumed is set */
    REQ_INCOMPLETE, /* need more bytes; *consumed untouched */
    REQ_ERROR /* malformed; *err is set */
} ParseResult;

typedef enum {
    REQ_ERR_NONE = 0,
    REQ_ERR_BAD_REQUEST_LINE,
    REQ_ERR_BAD_HEADER,
    REQ_ERR_HEADERS_TOO_LARGE, /* -> 431 */
    REQ_ERR_BODY_TOO_LARGE, /* -> 413 */
    REQ_ERR_TRANSFER_ENCODING, /* chunked rejected in v1 -> 411 */
    REQ_ERR_UNSUPPORTED_VERSION /* non-HTTP/1.x -> 505 */
} ParseError;

/* On REQ_INCOMPLETE, buf is unchanged and the caller appends more bytes
   and calls again. On REQ_OK, *consumed bytes were consumed and the
   caller must memmove the remainder before parsing the next request. */
ParseResult request_parse(Arena *a, const char *buf, size_t len,
                          size_t max_header_bytes, size_t max_body_bytes,
                          Request **out, size_t *consumed, ParseError *err);

#endif
