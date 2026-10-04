#include <dunlin/dunlin.h>

#include <stdlib.h>

#define BLOCK_SIZE 4096

typedef struct Block {
    struct Block *next;
    size_t used;
    size_t cap;
    char data[];
} Block;

struct Arena {
    Block *head;
};

static Block *new_block(size_t cap)
{
    Block *b = malloc(sizeof(Block) + cap);
    if (!b)
        return NULL;
    b->next = NULL;
    b->used = 0;
    b->cap = cap;
    return b;
}

Arena *arena_new(void)
{
    Arena *a = malloc(sizeof(*a));
    if (!a)
        return NULL;
    a->head = new_block(BLOCK_SIZE);
    if (!a->head) {
        free(a);
        return NULL;
    }
    return a;
}

void *arena_alloc(Arena *a, size_t n)
{
    size_t aligned;
    Block *b;

    if (!a || n == 0)
        return NULL;

    aligned = (n + 7) & ~(size_t)7;
    b = a->head;
    if (b->used + aligned > b->cap) {
        size_t cap = aligned > BLOCK_SIZE ? aligned : BLOCK_SIZE;
        Block *nb = new_block(cap);
        if (!nb)
            return NULL;
        nb->next = b;
        a->head = nb;
        b = nb;
    }
    void *p = b->data + b->used;
    b->used += aligned;
    return p;
}
