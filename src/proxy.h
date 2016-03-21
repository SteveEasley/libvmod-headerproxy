#ifndef PROXY_H
#define PROXY_H

#include <time.h>
#include <syslog.h>

#include "vrt.h"
#include "vcl.h"
#include "vsb.h"
#include "cache/cache.h"

#include "jsmn.h"

#define PROXY_CONNECT_TIMEOUT   -1
#define PROXY_TIMEOUT           -1
#define PROXY_POOL_MAX          5000    /* Same as varnish max threads */

#define PROXY_NAME              "libvmod-headerproxy"
#define PROXY_HEADER            "X-Vmod-HeaderProxy"

#define JSON_MAX_TOKENS         32

struct proxy_config {
    unsigned  magic;
#define PROXY_CONFIG_MAGIC 0xDBC2C13C
    char     *url;
    char     *host;
    long     *connect_timeout;
    long     *timeout;
};

struct proxy_request {
    unsigned magic;
#define PROXY_REQUEST_MAGIC 0xFBA1C37A
    unsigned                    id;
    VTAILQ_ENTRY(proxy_request) list;
    const struct vrt_ctx        *ctx;
    time_t                      ts;     /* Timestamp for GC */
    short                       busy;
    struct proxy_config         cfg;
    unsigned                    methods;
    jsmntok_t                   json_toks[JSON_MAX_TOKENS];
    uint16_t                    json_toks_len;
    struct vsb                  *body;
    char                        *error;
};

#ifdef DEBUG
#define PROXY_DEBUG(ctx, m, ...) \
    do { \
        VSLb(ctx->vsl, SLT_Debug, PROXY_NAME ": " m, __VA_ARGS__); \
        syslog(LOG_ERR, PROXY_NAME ": " m, __VA_ARGS__); \
    } while (0)
#else
#define PROXY_DEBUG(ctx, m, ...)
#endif

#define PROXY_LOG(ctx, m, ...) \
    do { \
        VSLb(ctx->vsl, SLT_Debug, PROXY_NAME ": " m, __VA_ARGS__); \
    } while (0)

#define PROXY_WARN(ctx, m, ...) \
    do { \
        VSLb(ctx->vsl, SLT_Error, PROXY_NAME ": " m, __VA_ARGS__); \
        syslog(LOG_ERR, PROXY_NAME ": " m, __VA_ARGS__); \
    } while (0)

#define PROXY_ERROR_INT(ctx, m, ...) \
    do { \
        PROXY_WARN(ctx, m, __VA_ARGS__); \
        return -1; \
    } while (0)

#define PROXY_REQ_ERROR_INT(req, m, ...) \
    do { \
        AN(req->ctx); \
        req->error = WS_Printf(req->ctx->ws, m, __VA_ARGS__); \
        PROXY_WARN(req->ctx, m, __VA_ARGS__); \
        req->ctx = NULL; \
        return -1; \
    } while (0)

#define PROXY_ERROR_VOID(ctx, m, ...) \
    do { \
        PROXY_WARN(ctx, m, __VA_ARGS__); \
        return; \
    } while (0)

#define PROXY_REQ_ERROR_VOID(req, m, ...) \
    do { \
        AN(req->ctx); \
        req->error = WS_Printf(req->ctx->ws, m, __VA_ARGS__); \
        PROXY_WARN(req->ctx, m, __VA_ARGS__); \
        req->ctx = NULL; \
        return; \
    } while (0)

#define PROXY_ERROR_NULL(ctx, m, ...) \
    do { \
        PROXY_WARN(ctx, m, __VA_ARGS__); \
        return NULL; \
    } while (0)

struct proxy_config *
proxy_init();

struct proxy_request *
proxy_get_request(const struct vrt_ctx *ctx, const struct proxy_config *cfg);

struct proxy_request *
proxy_resume_request(const struct vrt_ctx *ctx);

void
proxy_release_request(const struct vrt_ctx *ctx, struct proxy_request *req);

void
proxy_curl(const struct vrt_ctx *ctx, struct proxy_request *req);

void
proxy_add_headers(const struct vrt_ctx *ctx, struct proxy_request *req);

void
proxy_free_config(void *p);

#endif
