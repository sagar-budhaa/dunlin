/* Private to libdunlin.a. Not installed, not part of the public API. */
#ifndef DUNLIN_INTERNAL_H
#define DUNLIN_INTERNAL_H

#include <dunlin/dunlin.h>

/* header.c */
typedef void (*header_visit)(const char *key, const char *value, void *ctx);
void header_each(const Header *h, header_visit fn, void *ctx);

/* writer.c: concrete writer buffering into the request arena. */
ResponseWriter writer_new(Arena *a);
int writer_serialize(ResponseWriter *w, Arena *scratch,
                     const char **out, size_t *len);

#endif
