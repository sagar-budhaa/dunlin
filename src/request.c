#include <dunlin/dunlin.h>

#include <string.h>

struct Request {
    Arena *arena;
    char *method;
    char *path;
    char *query;
    char *proto;
    Header *header;
    char *body;
    size_t body_len;
    const char *remote_addr; /* set by conn, NULL until then */
};

static const char *find_crlf(const char *p, const char *end)
{
    while (p + 1 < end) {
        if (p[0] == '\r' && p[1] == '\n')
            return p;
        p++;
    }
    return NULL;
}

static const char *find_hdr_end(const char *p, const char *end)
{
    while (p + 3 < end) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')
            return p;
        p++;
    }
    return NULL;
}

static int is_http1(const char *p, size_t n)
{
    return (n == 8 && memcmp(p, "HTTP/1.0", 8) == 0) ||
           (n == 8 && memcmp(p, "HTTP/1.1", 8) == 0);
}

static int token_is(const char *tok, size_t n, const char *word)
{
    size_t i = 0;
    while (i < n && word[i]) {
        char c = tok[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != word[i])
            return 0;
        i++;
    }
    return i == n && word[i] == '\0';
}

ParseResult request_parse(Arena *a, const char *buf, size_t len,
                          size_t max_header_bytes, size_t max_body_bytes,
                          Request **out, size_t *consumed, ParseError *err)
{
    const char *end = buf + len;
    const char *line_end, *sp1, *sp2, *term;
    const char *target, *ver;
    size_t method_len, target_len, ver_len;
    const char *qs;
    Request *r;
    const char *body_start;
    size_t content_len = 0;

    if (!a || !buf || !out || !consumed || !err)
        return REQ_ERROR;

    line_end = find_crlf(buf, end);
    if (!line_end)
        return REQ_INCOMPLETE;

    sp1 = memchr(buf, ' ', (size_t)(line_end - buf));
    if (!sp1 || sp1 == buf) {
        *err = REQ_ERR_BAD_REQUEST_LINE;
        return REQ_ERROR;
    }
    sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - (sp1 + 1)));
    if (!sp2 || sp2 == sp1 + 1) {
        *err = REQ_ERR_BAD_REQUEST_LINE;
        return REQ_ERROR;
    }

    method_len = (size_t)(sp1 - buf);
    target = sp1 + 1;
    target_len = (size_t)(sp2 - target);
    ver = sp2 + 1;
    ver_len = (size_t)(line_end - ver);
    if (ver_len == 0) {
        *err = REQ_ERR_BAD_REQUEST_LINE;
        return REQ_ERROR;
    }
    if (!is_http1(ver, ver_len)) {
        *err = REQ_ERR_UNSUPPORTED_VERSION;
        return REQ_ERROR;
    }

    term = find_hdr_end(line_end, end);
    if (!term) {
        if (len > max_header_bytes) {
            *err = REQ_ERR_HEADERS_TOO_LARGE;
            return REQ_ERROR;
        }
        return REQ_INCOMPLETE;
    }
    if ((size_t)((term + 4) - buf) > max_header_bytes) {
        *err = REQ_ERR_HEADERS_TOO_LARGE;
        return REQ_ERROR;
    }

    r = arena_calloc(a, sizeof(*r));
    if (!r) {
        /* TODO: no OOM code in ParseError; conn should send 500 here. */
        *err = REQ_ERR_NONE;
        return REQ_ERROR;
    }
    r->arena = a;
    r->method = arena_strndup(a, buf, method_len);

    qs = memchr(target, '?', target_len);
    if (qs) {
        r->path = arena_strndup(a, target, (size_t)(qs - target));
        r->query = arena_strndup(a, qs + 1, (size_t)(target_len - (qs + 1 - target)));
    } else {
        r->path = arena_strndup(a, target, target_len);
        r->query = arena_strdup(a, "");
    }
    r->proto = arena_strndup(a, ver, ver_len);
    r->header = header_new(a);
    r->body = NULL;
    r->body_len = 0;
    if (!r->method || !r->path || !r->query || !r->proto || !r->header) {
        *err = REQ_ERR_NONE;
        return REQ_ERROR;
    }

    const char *cur = line_end + 2;
    while (cur < term) {
        const char *eol = find_crlf(cur, term + 2);
        const char *colon, *name_end, *val, *val_end;
        char *k, *v;
        if (!eol || eol > term)
            eol = term;
        if (eol == cur)
            break;
        if (*cur == ' ' || *cur == '\t') {
            *err = REQ_ERR_BAD_HEADER;
            return REQ_ERROR;
        }
        colon = memchr(cur, ':', (size_t)(eol - cur));
        if (!colon) {
            *err = REQ_ERR_BAD_HEADER;
            return REQ_ERROR;
        }
        name_end = colon;
        while (name_end > cur && (name_end[-1] == ' ' || name_end[-1] == '\t'))
            name_end--;
        if (name_end == cur) {
            *err = REQ_ERR_BAD_HEADER;
            return REQ_ERROR;
        }
        for (const char *c = cur; c < name_end; c++) {
            if (*c == ' ' || *c == '\t') {
                *err = REQ_ERR_BAD_HEADER;
                return REQ_ERROR;
            }
        }
        val = colon + 1;
        while (val < eol && (*val == ' ' || *val == '\t'))
            val++;
        val_end = eol;
        while (val_end > val && (val_end[-1] == ' ' || val_end[-1] == '\t'))
            val_end--;
        k = arena_strndup(a, cur, (size_t)(name_end - cur));
        v = arena_strndup(a, val, (size_t)(val_end - val));
        if (!k || !v || header_add(r->header, k, v) != 0) {
            *err = REQ_ERR_NONE;
            return REQ_ERROR;
        }
        cur = eol + 2;
    }

    const char *te = header_get(r->header, "Transfer-Encoding");
    if (te) {
        /* v1 rejects chunked; identity is the only valid use. */
        const char *p = te;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == ',')
                p++;
            if (*p == '\0')
                break;
            const char *tok = p;
            while (*p && *p != ' ' && *p != '\t' && *p != ',')
                p++;
            size_t n = (size_t)(p - tok);
            if (!token_is(tok, n, "identity")) {
                *err = REQ_ERR_TRANSFER_ENCODING;
                return REQ_ERROR;
            }
        }
    }

    const char *cl = header_get(r->header, "Content-Length");
    if (cl) {
        if (*cl == '\0') {
            *err = REQ_ERR_BAD_HEADER;
            return REQ_ERROR;
        }
        for (const char *c = cl; *c; c++) {
            if (*c < '0' || *c > '9') {
                *err = REQ_ERR_BAD_HEADER;
                return REQ_ERROR;
            }
        }
        unsigned long n = 0;
        for (const char *c = cl; *c; c++)
            n = n * 10 + (unsigned long)(*c - '0');
        if (n > max_body_bytes) {
            *err = REQ_ERR_BODY_TOO_LARGE;
            return REQ_ERROR;
        }
        content_len = (size_t)n;
    }

    body_start = term + 4;
    if ((size_t)(end - body_start) < content_len)
        return REQ_INCOMPLETE;
    if (content_len > 0) {
        r->body = arena_alloc(a, content_len);
        if (!r->body) {
            *err = REQ_ERR_NONE;
            return REQ_ERROR;
        }
        memcpy(r->body, body_start, content_len);
        r->body_len = content_len;
    }

    *out = r;
    *consumed = (size_t)((body_start + content_len) - buf);
    return REQ_OK;
}

const char *req_method(Request *r)
{
    return r ? r->method : NULL;
}

const char *req_path(Request *r)
{
    return r ? r->path : NULL;
}

const char *req_query(Request *r)
{
    return r ? r->query : NULL;
}

const char *req_proto(Request *r)
{
    return r ? r->proto : NULL;
}

Header *req_header(Request *r)
{
    return r ? r->header : NULL;
}

const char *req_host(Request *r)
{
    if (!r)
        return NULL;
    return header_get(r->header, "Host");
}

const char *req_body(Request *r, size_t *len)
{
    if (!r)
        return NULL;
    if (len)
        *len = r->body_len;
    return r->body;
}

static const char *scan_params(Arena *a, const char *p, size_t n, const char *name)
{
    size_t namelen = strlen(name);

    while (n > 0) {
        const char *amp = memchr(p, '&', n);
        size_t pair = amp ? (size_t)(amp - p) : n;
        const char *eq = memchr(p, '=', pair);
        size_t klen = eq ? (size_t)(eq - p) : pair;
        if (klen == namelen && memcmp(p, name, namelen) == 0) {
            if (!eq)
                return "";
            return arena_strndup(a, eq + 1, pair - (klen + 1));
        }
        if (!amp)
            break;
        p = amp + 1;
        n -= pair + 1;
    }
    return NULL;
}

const char *req_query_value(Request *r, const char *name)
{
    if (!r || !name)
        return NULL;
    return scan_params(r->arena, r->query, strlen(r->query), name);
}

const char *req_form_value(Request *r, const char *name)
{
    const char *ct, *v;

    if (!r || !name)
        return NULL;
    /* body params win over query ones for form methods. */
    if (r->body_len > 0 && (strcmp(r->method, "POST") == 0 ||
                            strcmp(r->method, "PUT") == 0 ||
                            strcmp(r->method, "PATCH") == 0)) {
        ct = header_get(r->header, "Content-Type");
        if (ct) {
            size_t n = strcspn(ct, ";");
            while (n > 0 && (ct[n - 1] == ' ' || ct[n - 1] == '\t'))
                n--;
            if (token_is(ct, n, "application/x-www-form-urlencoded")) {
                v = scan_params(r->arena, r->body, r->body_len, name);
                if (v)
                    return v;
            }
        }
    }
    return req_query_value(r, name);
}

const char *req_path_value(Request *r, const char *name)
{
    (void)r;
    (void)name;
    return NULL; /* mux fills path params in step 5 */
}

const char *req_remote_addr(Request *r)
{
    return r ? r->remote_addr : NULL;
}

Arena *req_scratch(Request *r)
{
    return r ? r->arena : NULL;
}
