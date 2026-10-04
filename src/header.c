#include <dunlin/dunlin.h>

typedef struct Entry {
    char *key;
    char *val;
    struct Entry *next;
} Entry;

struct Header {
    Arena *arena;
    Entry *head;
};

static int eq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

Header *header_new(Arena *a)
{
    Header *h;

    if (!a)
        return NULL;
    h = arena_calloc(a, sizeof(*h));
    if (!h)
        return NULL;
    h->arena = a;
    return h;
}

int header_add(Header *h, const char *key, const char *value)
{
    Entry *e;
    char *k, *v;

    if (!h || !key || !value)
        return -1;
    k = arena_strdup(h->arena, key);
    v = arena_strdup(h->arena, value);
    e = arena_alloc(h->arena, sizeof(*e));
    if (!k || !v || !e)
        return -1;
    e->key = k;
    e->val = v;
    e->next = NULL;
    if (!h->head) {
        h->head = e;
    } else {
        Entry *t = h->head;
        while (t->next)
            t = t->next;
        t->next = e;
    }
    return 0;
}

const char *header_get(const Header *h, const char *key){
    Entry *e;

    if (!h || !key)
        return NULL;
    for (e = h->head; e; e = e->next) {
        if (eq_ci(e->key, key))
            return e->val;
    }
    return NULL;
}

void header_del(Header *h, const char *key)
{
    Entry *prev = NULL, *e, *next;

    if (!h || !key)
        return;
    e = h->head;
    while (e) {
        next = e->next;
        if (eq_ci(e->key, key)) {
            if (prev)
                prev->next = next;
            else
                h->head = next;
        } else {
            prev = e;
        }
        e = next;
    }
}

int header_set(Header *h, const char *key, const char *value)
{
    if (!h || !key || !value)
        return -1;
    header_del(h, key);
    return header_add(h, key, value);
}

size_t header_values(const Header *h, const char *key, const char **out, size_t n)
{
    Entry *e;
    size_t count = 0;

    if (!h || !key)
        return 0;
    for (e = h->head; e; e = e->next) {
        if (eq_ci(e->key, key)) {
            if (out && count < n)
                out[count] = e->val;
            count++;
        }
    }
    return count;
}
