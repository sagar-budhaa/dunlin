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

#endif
