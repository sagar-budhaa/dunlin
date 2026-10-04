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
    Header *h = header_new(a);
    ASSERT(h != NULL);
    ASSERT(header_new(NULL) == NULL);

    ASSERT(header_set(h, "Content-Type", "text/plain") == 0);
    ASSERT(strcmp(header_get(h, "Content-Type"), "text/plain") == 0);

    /* case-insensitive lookup */
    ASSERT(strcmp(header_get(h, "content-type"), "text/plain") == 0);
    ASSERT(strcmp(header_get(h, "CONTENT-TYPE"), "text/plain") == 0);

    /* add appends, set replaces */
    ASSERT(header_add(h, "X-Tag", "a") == 0);
    ASSERT(header_add(h, "X-Tag", "b") == 0);
    ASSERT(header_add(h, "x-tag", "c") == 0);
    ASSERT(strcmp(header_get(h, "X-Tag"), "a") == 0);

    const char *vals[4];
    ASSERT(header_values(h, "X-Tag", vals, 4) == 3);
    ASSERT(strcmp(vals[0], "a") == 0);
    ASSERT(strcmp(vals[1], "b") == 0);
    ASSERT(strcmp(vals[2], "c") == 0);

    /* small output array still reports the full count */
    ASSERT(header_values(h, "X-Tag", vals, 2) == 3);
    ASSERT(strcmp(vals[0], "a") == 0);
    ASSERT(strcmp(vals[1], "b") == 0);

    ASSERT(header_set(h, "X-Tag", "solo") == 0);
    ASSERT(strcmp(header_get(h, "X-Tag"), "solo") == 0);
    ASSERT(header_values(h, "X-Tag", vals, 4) == 1);

    header_del(h, "Content-Type");
    ASSERT(header_get(h, "Content-Type") == NULL);
    ASSERT(header_get(h, "nope") == NULL);
    ASSERT(header_values(h, "nope", vals, 4) == 0);

    header_del(h, "X-TAG");
    ASSERT(header_get(h, "X-Tag") == NULL);

    ASSERT(header_set(NULL, "K", "V") == -1);
    ASSERT(header_add(h, NULL, "V") == -1);

    arena_free(a);

    if (failures == 0)
        printf("test_header: ok\n");
    return failures ? 1 : 0;
}
