#ifndef PROXY_H
#define PROXY_H

#include <time.h>
#include <syslog.h>

#include "vcl.h"
#include "vrt.h"
#include "vdef.h"
#include "vsb.h"
#include "cache/cache.h"
#include "cache/cache_director.h"
#include "cache/cache_backend.h"

#include "jsmn.h"

#define PROXY_CONNECT_TIMEOUT   -1
#define PROXY_TIMEOUT           -1
#define PROXY_POOL_MAX          5000    /* Same as varnish max threads */

#define PROXY_NAME              "libvmod-headerproxy"
#define PROXY_HEADER            "X-Vmod-HeaderProxy"

#define JSON_MAX_TOKENS         32

struct proxy_request {
    unsigned magic;
#define PROXY_REQUEST_MAGIC 0xFBA1C37A
    const struct vrt_ctx        *ctx;
    struct vsb                  *json;
    jsmntok_t                   json_toks[JSON_MAX_TOKENS];
    uint16_t                    json_toks_len;
    uint8_t                     collect_cookies;
    uint16_t                    restarts;
    char                        *error;
};

#ifdef DEBUG
#define PROXY_DEBUG(ctx, m, ...) \
    do { \
        VSLb(ctx->vsl, SLT_Debug, PROXY_NAME ": " m, __VA_ARGS__); \
    } while (0)
#else
#define PROXY_DEBUG(ctx, m, ...)
#endif

#define PROXY_LOG(ctx, m, ...) \
    do { \
        VSLb(ctx->vsl, SLT_VCL_Log, PROXY_NAME ": " m, __VA_ARGS__); \
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

void
proxy_init();

struct proxy_request *
proxy_create_request(VRT_CTX);

void
proxy_restart_request(VRT_CTX, struct proxy_request *req);

void
proxy_release_request(void *ptr);

void
proxy_curl(VRT_CTX, struct proxy_request *req, const struct director *dir,
            const char *path);

void
proxy_process_request(VRT_CTX, struct proxy_request *req);

#endif
