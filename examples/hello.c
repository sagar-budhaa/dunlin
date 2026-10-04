#include <dunlin/dunlin.h>

#include <stdio.h>

static void hello(ResponseWriter *w, Request *r, void *ctx)
{
    (void)ctx;
    rw_printf(w, "hello, %s\n", req_path_value(r, "name"));
}

int main(void)
{
    ServeMux *m = mux_new();
    if (!m)
        return 1;
    mux_handle_func(m, "GET /hello/{name}", hello, NULL);

    Server s = { 0 };
    s.addr = ":8080";
    s.handler = mux_handler(m);
    s.read_timeout_ms = 5000;
    s.idle_timeout_ms = 30000;
    if (server_listen_and_serve(&s) != 0) {
        fprintf(stderr, "serve failed\n");
        return 1;
    }
    return 0;
}
