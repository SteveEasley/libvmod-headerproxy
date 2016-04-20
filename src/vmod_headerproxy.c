#include <stdio.h>
#include <stdlib.h>

#include "proxy.h"

#include "vcc_if.h"

int
init_function(VRT_CTX, struct vmod_priv *priv, enum vcl_event_e e)
{
    if (e != VCL_EVENT_LOAD)
      return 0;

    proxy_init();

    return 0;
}

VCL_VOID
vmod_backend(VRT_CTX, VCL_BACKEND backend)
{
    if (ctx->method == VCL_MET_RECV) {
        struct proxy_request *req = proxy_get_request(ctx, 1);

        if (VALID_OBJ(req, PROXY_REQUEST_MAGIC))
            req->backend = backend;
    }
    else
        PROXY_ERROR_VOID(ctx,
            "invalid call outside vcl_recv%s", "");
}

VCL_VOID
vmod_path(const struct vrt_ctx *ctx, VCL_STRING path)
{
    if (ctx->method == VCL_MET_RECV) {
        struct proxy_request *req = proxy_get_request(ctx, 1);

        if (VALID_OBJ(req, PROXY_REQUEST_MAGIC))
            req->path = path;
    }
    else
        PROXY_ERROR_VOID(ctx,
            "invalid call outside vcl_recv%s", "");
}

VCL_VOID
vmod_call(VRT_CTX)
{
    if (ctx->method != VCL_MET_RECV)
        return;

    struct proxy_request *req = proxy_get_request(ctx, 1);

    if (VALID_OBJ(req, PROXY_REQUEST_MAGIC)) {
        proxy_curl(ctx, req);
        proxy_add_headers(ctx, req);
    }
}

VCL_VOID
vmod_process(VRT_CTX)
{
    struct proxy_request *req;

    switch (ctx->method) {
        case VCL_MET_BACKEND_FETCH:
        case VCL_MET_BACKEND_RESPONSE:
        case VCL_MET_DELIVER:
            req = proxy_resume_request(ctx);
            if (VALID_OBJ(req, PROXY_REQUEST_MAGIC)) {
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
            if (VALID_OBJ(req, PROXY_REQUEST_MAGIC))
                proxy_release_request(ctx, req);

            break;

        default:
            break;
    }
}

VCL_STRING
vmod_error(VRT_CTX)
{
    if (ctx->method != VCL_MET_RECV)
        return NULL;

    struct proxy_request *req = proxy_get_request(ctx, 0);
    CHECK_OBJ_ORNULL(req, PROXY_REQUEST_MAGIC);

    if (req == NULL)
        return NULL;

    return req->error;
}
