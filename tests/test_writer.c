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

int main(void)
{
    Arena *a = arena_new();
    ASSERT(a != NULL);

    ResponseWriter w = writer_new(a);
    ASSERT(rw_write_string(&w, "hi") == 2);
    ASSERT(w.status == 200);
    ASSERT(w.written == 2);

    Arena *scratch = arena_new();
    const char *out = NULL;
    size_t len = 0;
    ASSERT(writer_serialize(&w, scratch, &out, &len) == 0);
    ASSERT(strcmp(out, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi") == 0);

    arena_reset(a);
    arena_reset(scratch);

    ResponseWriter w2 = writer_new(a);
    header_set(rw_header(&w2), "Content-Type", "application/json");
    rw_write_header(&w2, 201);
    header_set(rw_header(&w2), "X-Late", "nope");
    rw_printf(&w2, "{\"id\":%d}", 1);
    ASSERT(w2.status == 201);
    rw_write_header(&w2, 500);
    ASSERT(w2.status == 201);
    ASSERT(writer_serialize(&w2, scratch, &out, &len) == 0);
    ASSERT(strstr(out, "HTTP/1.1 201 Created\r\n") == out);
    ASSERT(strstr(out, "Content-Type: application/json\r\n") != NULL);
    ASSERT(strstr(out, "X-Late") == NULL);
    ASSERT(strstr(out, "\r\n\r\n{\"id\":1}") != NULL);

    arena_reset(a);
    arena_reset(scratch);

    ResponseWriter w3 = writer_new(a);
    ASSERT(writer_serialize(&w3, scratch, &out, &len) == 0);
    ASSERT(strcmp(out, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n") == 0);

    arena_free(scratch);
    arena_free(a);

    if (failures == 0)
        printf("test_writer: ok\n");
    return failures ? 1 : 0;
}
