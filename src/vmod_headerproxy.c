#include <stdio.h>
#include <stdlib.h>

#include "proxy.h"

#include "vcc_if.h"

int
init_function(struct vmod_priv *priv, const struct VCL_conf *conf)
{
    struct proxy_config *cfg = proxy_init();
    CHECK_OBJ_NOTNULL(cfg, PROXY_CONFIG_MAGIC);

    priv->priv = (void *)cfg;
    priv->free = proxy_free_config;

    return 0;
}

VCL_VOID
vmod_url(const struct vrt_ctx *ctx, struct vmod_priv *priv,
         VCL_STRING url)
{
    size_t len = strlen(url) + 1;

    struct proxy_config *cfg = (struct proxy_config *)priv->priv;
    CHECK_OBJ_NOTNULL(cfg, PROXY_CONFIG_MAGIC);

    if (ctx->method == VCL_MET_INIT) {
        if (cfg->url)
            free(cfg->url);

        cfg->url = malloc(len);
        AN(cfg->url);

        memcpy(cfg->url, url, len);
    }
    else if (ctx->method == VCL_MET_RECV) {
        struct proxy_request *req = proxy_get_request(ctx, cfg);
        CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

        if (req == NULL)
            return;

        req->cfg.url = WS_Alloc(ctx->ws, len);
        AN(req->cfg.url);

        memcpy(req->cfg.url, url, len);

        PROXY_DEBUG(ctx, "(%x) set url (%s)", req->id, req->cfg.url);
    }
    else
        PROXY_ERROR_VOID(ctx,
            "invalid call outside vcl_init or vcl_recv%s", "");
}

VCL_VOID
vmod_host(const struct vrt_ctx *ctx, struct vmod_priv *priv,
         VCL_STRING host)
{
    size_t len = strlen(host) + 1;

    struct proxy_config *cfg = (struct proxy_config *)priv->priv;
    CHECK_OBJ_NOTNULL(cfg, PROXY_CONFIG_MAGIC);

    if (ctx->method == VCL_MET_INIT) {
        if (cfg->host)
            free(cfg->host);

        cfg->host = malloc(len);
        AN(cfg->host);

        memcpy(cfg->host, host, len);
    }
    else if (ctx->method == VCL_MET_RECV) {
        struct proxy_request *req = proxy_get_request(ctx, cfg);
        CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

        if (req == NULL)
            return;

        req->cfg.host = WS_Alloc(ctx->ws, len);
        AN(req->cfg.host);

        memcpy(req->cfg.host, host, len);

        PROXY_DEBUG(ctx, "(%x) set host (%s)", req->id, req->cfg.host);
    }
    else
        PROXY_ERROR_VOID(ctx,
            "invalid call outside vcl_init or vcl_recv%s", "");
}

VCL_VOID
vmod_connect_timeout(const struct vrt_ctx *ctx, struct vmod_priv *priv,
                     VCL_INT connect_timeout)
{
    size_t len = sizeof(connect_timeout);

    struct proxy_config *cfg = (struct proxy_config *)priv->priv;
    CHECK_OBJ_NOTNULL(cfg, PROXY_CONFIG_MAGIC);

    if (ctx->method == VCL_MET_INIT) {
        if (cfg->connect_timeout)
            free(cfg->connect_timeout);

        cfg->connect_timeout = malloc(len);
        AN(cfg->connect_timeout);

        memcpy(cfg->connect_timeout, &connect_timeout, len);
    }
    else if (ctx->method == VCL_MET_RECV) {
        struct proxy_request *req = proxy_get_request(ctx, cfg);
        CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

        if (req == NULL)
            return;

        req->cfg.connect_timeout = (long *)WS_Alloc(ctx->ws, len);
        AN(req->cfg.connect_timeout);

        memcpy(req->cfg.connect_timeout, &connect_timeout, len);

        PROXY_DEBUG(ctx, "(%x) set connect_timeout (%s)", req->id, req->cfg.url);
    }
    else
        PROXY_ERROR_VOID(ctx,
            "invalid call outside vcl_init or vcl_recv%s", "");
}

VCL_VOID
vmod_timeout(const struct vrt_ctx *ctx, struct vmod_priv *priv,
             VCL_INT timeout)
{
    size_t len = sizeof(timeout);

    struct proxy_config *cfg = (struct proxy_config *)priv->priv;
    CHECK_OBJ_NOTNULL(cfg, PROXY_CONFIG_MAGIC);

    if (ctx->method == VCL_MET_INIT) {
        if (cfg->timeout)
            free(cfg->timeout);

        cfg->timeout = malloc(len);
        AN(cfg->timeout);

        memcpy(cfg->timeout, &timeout, len);
    }
    else if (ctx->method == VCL_MET_RECV) {
        struct proxy_request *req = proxy_get_request(ctx, cfg);
        CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

        if (req == NULL)
            return;

        req->cfg.timeout = (long *)WS_Alloc(ctx->ws, len);
        AN(req->cfg.timeout);

        memcpy(req->cfg.timeout, &timeout, len);

        PROXY_DEBUG(ctx, "(%x) set timeout (%s)", req->id, req->cfg.url);
    }
    else
        PROXY_ERROR_VOID(ctx,
                         "invalid call outside vcl_init or vcl_recv%s", "");
}

VCL_VOID
vmod_call(const struct vrt_ctx *ctx, struct vmod_priv *priv)
{
    if (ctx->method != VCL_MET_RECV)
        return;

    struct proxy_config *cfg = (struct proxy_config *)priv->priv;
    CHECK_OBJ_NOTNULL(cfg, PROXY_CONFIG_MAGIC);

    struct proxy_request *req = proxy_get_request(ctx, cfg);

    if (req && req->cfg.url) {
        proxy_curl(ctx, req);
        proxy_add_headers(ctx, req);
    }
}

VCL_VOID
vmod_process(const struct vrt_ctx *ctx)
{
    struct proxy_request *req;

    switch (ctx->method) {
        case VCL_MET_BACKEND_FETCH:
        case VCL_MET_BACKEND_RESPONSE:
        case VCL_MET_DELIVER:
            req = proxy_resume_request(ctx);
            CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

            if (req) {
                proxy_add_headers(ctx, req);

                if (ctx->method == VCL_MET_DELIVER ||
                    ctx->method == VCL_MET_BACKEND_RESPONSE)
                    proxy_release_request(ctx, req);
            }

            break;

        case VCL_MET_PIPE:
        case VCL_MET_SYNTH:
        case VCL_MET_BACKEND_ERROR:
            req = proxy_resume_request(ctx);
            CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

            if (req)
                proxy_release_request(ctx, req);

            break;

        default:
            break;
    }
}

VCL_STRING
vmod_error(const struct vrt_ctx *ctx)
{
    if (ctx->method != VCL_MET_RECV)
        return NULL;

    struct proxy_request *req = proxy_get_request(ctx, NULL);
    CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

    if (req == NULL)
        return NULL;

    return req->error;
}
