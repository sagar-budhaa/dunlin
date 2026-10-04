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

#endif
