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

    void *p1 = arena_alloc(a, 1);
    ASSERT(p1 != NULL);
    ASSERT(((size_t)p1 & 7) == 0);

    void *p2 = arena_alloc(a, 16);
    ASSERT(p2 != NULL);
    ASSERT(p2 != p1);

    ASSERT(arena_alloc(NULL, 8) == NULL);
    ASSERT(arena_alloc(a, 0) == NULL);

    char *z = arena_calloc(a, 32);
    ASSERT(z != NULL);
    for (int i = 0; i < 32; i++)
        ASSERT(z[i] == 0);

    char *s = arena_strdup(a, "hello");
    ASSERT(s != NULL);
    ASSERT(strcmp(s, "hello") == 0);

    char *t = arena_strndup(a, "hello world", 5);
    ASSERT(t != NULL);
    ASSERT(strcmp(t, "hello") == 0);
    ASSERT(arena_strdup(a, NULL) == NULL);

    for (int i = 0; i < 10000; i++) {
        void *p = arena_alloc(a, 8);
        ASSERT(p != NULL);
        if (!p)
            break;
    }

    arena_reset(a);
    void *pr = arena_alloc(a, 16);
    ASSERT(pr != NULL);

    char *after = arena_strdup(a, "after-reset");
    ASSERT(after != NULL);
    ASSERT(strcmp(after, "after-reset") == 0);

    arena_free(a);

    if (failures == 0)
        printf("test_arena: ok\n");
    return failures ? 1 : 0;
}
