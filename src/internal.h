/* Private to libdunlin.a. Not installed, not part of the public API. */
#ifndef DUNLIN_INTERNAL_H
#define DUNLIN_INTERNAL_H

#include <dunlin/dunlin.h>

#include <setjmp.h>
#include <signal.h>

/* header.c */
typedef void (*header_visit)(const char *key, const char *value, void *ctx);
void header_each(const Header *h, header_visit fn, void *ctx);

/* writer.c: concrete writer buffering into the request arena. */
ResponseWriter writer_new(Arena *a);
int writer_serialize(ResponseWriter *w, Arena *scratch,
                     const char **out, size_t *len);

/* request.c: mux stores captures; values point into the request path
   (request arena), names into the mux pattern (mux arena). */
void request_set_path_param(Request *r, const char *name,
                            const char *val, size_t vlen);

/* request.c: per-request recover slot. conn owns the slot and sets it
   before dispatch; recover reads it back. Falls back to a stack slot
   when no conn is involved (tests). Plain jmp_buf for portability;
   server installs crash signals with SA_NODEFER so no mask is left
   blocked across the longjmp. */
typedef struct RecoverSlot {
    jmp_buf jb;
    sig_atomic_t failed;
} RecoverSlot;

void request_set_recover_slot(Request *r, RecoverSlot *slot);
RecoverSlot *request_recover_slot(Request *r);

#endif
