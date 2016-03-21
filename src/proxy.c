#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/errno.h>
#include <curl/curl.h>

#include "proxy.h"

static void
set_header_id(const struct vrt_ctx *ctx, const struct proxy_request *req);

static unsigned
get_header_id(const struct vrt_ctx *ctx);

static int
is_header(const txt *hh, const char *hdr);

static void
gc_request_pool();

static size_t
curl_recv(void *ptr, size_t size, size_t nmemb, void *ud);

static short
process_json(struct proxy_request *req, unsigned short *idx,
             unsigned *type, unsigned short lvl);

static VTAILQ_HEAD(,proxy_request) req_pool = VTAILQ_HEAD_INITIALIZER(req_pool);
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static short initialize = 1;
static short gc_running = 0;

struct proxy_config *
proxy_init()
{
    struct proxy_config *cfg;

    int connect_timeout = PROXY_CONNECT_TIMEOUT;
    int timeout = PROXY_TIMEOUT;

    ALLOC_OBJ(cfg, PROXY_CONFIG_MAGIC);
    AN(cfg);

    cfg->url = NULL;
    cfg->host = NULL;

    cfg->connect_timeout = (long *)malloc(sizeof(connect_timeout));
    AN(cfg->connect_timeout);
    memcpy(cfg->connect_timeout, &connect_timeout, sizeof(connect_timeout));

    cfg->timeout = (long *)malloc(sizeof(timeout));
    AN(cfg->timeout);
    memcpy(cfg->timeout, &timeout, sizeof(timeout));

    if (initialize)
        curl_global_init(CURL_GLOBAL_ALL);
    initialize = 0;

    return cfg;
}

void
proxy_free_config(void *p)
{
    struct proxy_config *cfg;
    CAST_OBJ(cfg, p, PROXY_CONFIG_MAGIC);

    if (cfg) {
        if (cfg->url)
            free(cfg->url);

        if (cfg->host)
            free(cfg->host);

        free(cfg->connect_timeout);
        free(cfg->timeout);

        free(cfg);
    }
}

struct proxy_request *
proxy_get_request(const struct vrt_ctx *ctx, const struct proxy_config *cfg)
{
    struct proxy_request *req = NULL, *preq = NULL;

    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
    CHECK_OBJ_ORNULL(cfg, PROXY_CONFIG_MAGIC);

    AN(ctx->req->vsl->wid);
    assert(ctx->method == VCL_MET_RECV);

    time_t ts = time(NULL);
    unsigned vxid = ctx->req->vsl->wid;
    unsigned pcount = 0;

    if (ctx->req->esi_level || ctx->req->restarts)
        return NULL;

    PROXY_DEBUG(ctx, "(%x) proxy_get_request", vxid);

    /* Check if previous vmod method call has already allocated a vmod
     * from pool request. Using vxid saves having to do a PROXY_HEADER
     * header lookup. */
    VTAILQ_FOREACH(preq, &req_pool, list) {
        pcount++; /* Track pool size for later */

        if (preq->id == vxid) {
            assert(req == NULL);

            req = preq;
            AN(req->busy);
            AZ(req->ctx);

            PROXY_DEBUG(ctx, "(%x) reloaded", req->id);
        }
    }

    if (req)
        return req;

    /* No cfg means caller was only looking for allocated requests, and doesn't
     * want to allocate one if it didnt exist. For example headerproxy.error()
     * calls. */
    if (cfg == NULL)
        return NULL;

    PROXY_DEBUG(ctx, "pool size: %i", pcount);

    AZ(pthread_mutex_lock(&mtx));           /*  vvv MUTEX LOCK vvv */

    VTAILQ_FOREACH(preq, &req_pool, list) {
        if (preq->busy == 0) {
            req = preq;
            req->id = vxid;
            req->busy = 1;
            AZ(req->ctx);
            break;
        }
    }

    AZ(pthread_mutex_unlock(&mtx));         /* ^^^ MUTEX UNLOCK ^^^ */

    gc_request_pool();

    /* Conjure a new request */
    if (req == NULL) {
        if (pcount >= PROXY_POOL_MAX)
            PROXY_ERROR_NULL(ctx, "pool maxed at %i", pcount);

        ALLOC_OBJ(req, PROXY_REQUEST_MAGIC);
        AN(req);

        req->id = vxid;
        req->busy = 1;
        req->ctx = NULL;
        req->ts = ts;
        req->body = VSB_new_auto();

        AZ(pthread_mutex_lock(&mtx));       /*  vvv MUTEX LOCK vvv */

        VTAILQ_INSERT_TAIL(&req_pool, req, list);

        AZ(pthread_mutex_unlock(&mtx));     /* ^^^ MUTEX UNLOCK ^^^ */

        PROXY_DEBUG(ctx, "(%x) created", req->id);
    }
    else
        PROXY_DEBUG(ctx, "(%x) reused", req->id);

    req->busy = 1;
    req->ts = ts;
    req->methods = 0;

    set_header_id(ctx, req);

    /* Copy default global config */
    memcpy(&req->cfg, cfg, sizeof(*cfg));

    return req;
}

/**
 * Resumes a vmod request already associated in proxy_get_request()
 */
struct proxy_request *
proxy_resume_request(const struct vrt_ctx *ctx)
{
    struct proxy_request *req = NULL, *preq = NULL;

    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

#ifdef DEBUG
    char *mn = NULL;

    switch (ctx->method) {
        case VCL_MET_BACKEND_FETCH:
            mn = "befetch";
            break;
        case VCL_MET_BACKEND_RESPONSE:
            mn = "beresp";
            break;
        case VCL_MET_BACKEND_ERROR:
            mn = "beerror";
            break;
        case VCL_MET_DELIVER:
            mn = "deliv";
            break;
        case VCL_MET_PIPE:
            mn = "pipe";
            break;
        case VCL_MET_SYNTH:
            mn = "synth";
            break;
        default:
            mn = "";
    }

    (void)mn;
#endif

    unsigned id = get_header_id(ctx);

    if (id <= 0)
        return NULL;

    VTAILQ_FOREACH(preq, &req_pool, list) {
        if (preq->id == id) {
            req = preq;
            break;
        }
    }

    if (req) {
        PROXY_DEBUG(ctx, "(%x) proxy_resume_request: %s", req->id, mn);

        AN(req->busy);
        AZ(req->ctx);

        req->methods |= ctx->method;
        req->ts = time(NULL);

        return req;
    }

    return NULL;
}

void
proxy_release_request(const struct vrt_ctx *ctx, struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    CHECK_OBJ_NOTNULL(req->body, VSB_MAGIC);
    AN(req->busy);
    AZ(req->ctx);

    switch (ctx->method) {
        case VCL_MET_BACKEND_RESPONSE:
            /* If vcl_deliver was already already called its an indication this
             * beresp is from a background fetch and we need to release. */
            if ((req->methods & VCL_MET_DELIVER) == 0)
                return;
            PROXY_DEBUG(ctx, "(%x) released (bgfetch)", req->id);
            break;

        case VCL_MET_DELIVER:
            /* If cached obj has expired we have to assume a background fetch
             * was or will be spawned, in which case we don't release */
            if (EXP_Ttl(ctx->req, &ctx->req->objcore->exp) < ctx->req->t_req) {
                PROXY_DEBUG(ctx, "(%x) blocked release (bgfetch)", req->id);
                return;
            }

        default:
            PROXY_DEBUG(ctx, "(%x) released", req->id);

    }

    // TODO: Remove proxy header so client doesnt get it

    req->id = 0;
    req->methods = 0;
    req->json_toks_len = 0;
    req->error = NULL;

    // Leave req->ts set to help gc_request_pool
    // No need to free req->error since its varnish ws managed

    VSB_clear(req->body);

    req->busy = 0;
}

#ifdef DEBUG
static int
curl_debug(CURL *ch, curl_infotype type, char *data, size_t size, void *ud)
{
    struct proxy_request *req;
    (void)ch;

    CAST_OBJ_NOTNULL(req, ud, PROXY_REQUEST_MAGIC);

    char *stype = NULL;
    switch (type) {
        case CURLINFO_TEXT:
            stype = "info"; break;
        case CURLINFO_HEADER_IN:
            stype = "in"; break;
        case CURLINFO_DATA_IN:
            stype = "body"; break;
        case CURLINFO_HEADER_OUT:
        case CURLINFO_DATA_OUT:
            stype = "out"; break;
        default:
            assert(0);
    }

    /* Trim newlines from ends */
    size_t len = size;
    for (char *p = data + size - 1; p >= data; p--) {
        if (*p < 0x20 || *p > 0x80)
            len--;
        else
            break;
    }

    if (len) {
        /* Convert \0 to printable */
        for (char *p = data + len - 1; p >= data; p--) {
            if (*p == 0)
                *p = '.';
        }

        (void)stype;
        PROXY_DEBUG(req->ctx, "(%x) curl %s: %.*s", req->id, stype,
                    (int)len, data);
    }

    return 0;
}
#endif

void
proxy_curl(const struct vrt_ctx *ctx, struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    AN(req->busy);

    PROXY_DEBUG(ctx, "(%x) proxy_curl", req->id);

    req->ctx = ctx;

    CURL *ch = curl_easy_init();
    AN(ch);

    curl_easy_setopt(ch, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(ch, CURLOPT_URL, req->cfg.url);
    curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_recv);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, req);

#ifdef DEBUG
    curl_easy_setopt(ch, CURLOPT_VERBOSE, DEBUG);
    curl_easy_setopt(ch, CURLOPT_DEBUGFUNCTION, curl_debug);
    curl_easy_setopt(ch, CURLOPT_DEBUGDATA, req);
#endif

    if (*req->cfg.connect_timeout > 0)
        curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, *req->cfg.connect_timeout);

    if (*req->cfg.timeout > 0)
        curl_easy_setopt(ch, CURLOPT_TIMEOUT, *req->cfg.timeout);

    struct curl_slist *headers = NULL;
    char *wshdr = NULL;

    if (req->cfg.host) {
        wshdr = WS_Printf(ctx->ws, "Host: %s", req->cfg.host);
        headers = curl_slist_append(headers, wshdr);
    }
    
    for (int u = 0; u < ctx->http_req->nhd; u++) {
        const txt hdr = ctx->http_req->hd[u];

        if (u == HTTP_HDR_METHOD) { /* GET, PUT, etc */
            wshdr = WS_Printf(ctx->ws, "X-Forwarded-Method: %s", hdr.b);
            headers = curl_slist_append(headers, wshdr);
        }
        else if (u == HTTP_HDR_URL) { /* /foo */
            wshdr = WS_Printf(ctx->ws, "X-Forwarded-Url: %s", hdr.b);
            headers = curl_slist_append(headers, wshdr);
        }
        else if (u == HTTP_HDR_PROTO) { /* HTTP/1.1 */
            wshdr = WS_Printf(ctx->ws, "Via: %s VMOD-HeaderProxy", hdr.b);
            headers = curl_slist_append(headers, wshdr);
        }
        else if (u >= HTTP_HDR_FIRST) {
            if (is_header(&hdr, H_Host)) {
                wshdr = WS_Printf(ctx->ws, "X-Forwarded-Host: %s", (hdr.b + H_Host[0] + 1));
                headers = curl_slist_append(headers, wshdr);
            }
            else if (is_header(&hdr, H_Via) || is_header(&hdr, H_Content_Length)) {
                continue;
            }
            else if (is_header(&hdr, H_Accept_Encoding)) {
                //curl_easy_setopt(ch, CURLOPT_ENCODING, "gzip");
                headers = curl_slist_append(headers, "Accept-Encoding: identity");
            }
            else
                headers = curl_slist_append(headers, hdr.b);
        }

        // TODO: Get CURLOPT_ENCODING to decode response. Currently broke
    }

    if (headers)
        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);

    PROXY_DEBUG(ctx, "(%x) curl url:%s", req->id, req->cfg.url);
    CURLcode ret = curl_easy_perform(ch);

    long status;
    curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status);

    if (headers)
        curl_slist_free_all(headers);

    curl_easy_cleanup(ch);

    VSB_finish(req->body);

    if (ret != 0)
        PROXY_REQ_ERROR_VOID(req, "(%x) curl err: %s", req->id,
                             curl_easy_strerror(ret));

    if (status != 200)
        PROXY_REQ_ERROR_VOID(req, "(%x) curl err: %lu response",
                             req->id, status);

    // TODO: check header content type

    char *json = VSB_data(req->body);
    size_t json_len = strlen(json);

    if (json_len == 0)
        PROXY_REQ_ERROR_VOID(req, "(%x) parse: no body", req->id);
    else if (json_len > 0x1FFFF)
        PROXY_REQ_ERROR_VOID(req, "(%x) parse: body too big (%zu)", req->id,
                             json_len);

    /* Save expensive json parse if doesnt open and close with {}  */
    char *p, fc = '\0', lc = '\0';
    for (p = json; p < (json + json_len); p++) {
        if (!isspace(*p)) {
            if (!fc)
                fc = *p;
            lc = *p;
        }
    }

    if ((fc != '{' && fc != '[') || (lc != '}' && lc != ']'))
        PROXY_REQ_ERROR_VOID(req, "(%x) parse2: failed to parse json", req->id);

    jsmn_parser parser;
    jsmn_init(&parser);

    req->json_toks_len = jsmn_parse(
        &parser, json, json_len, req->json_toks, JSON_MAX_TOKENS
    );

    if (req->json_toks_len < 0)
        PROXY_REQ_ERROR_VOID(req, "(%x) parse: failed to parse json", req->id);

    req->ctx = NULL;
    // int l;
    // char t[255];
    // for (int a = 0; a < request_toks_len; a++) {
    //     l = req->json_toks[a].end - req->json_toks[a].start;
    //     memcpy(t, json + req->json_toks[a].start, (l < 255 ? l : 255));
    //     t[l] = '\0';
    //     PROXY_DEBUG(ctx, "parse_response idx=%i size:%i type=%i val:%s",
    //         a, req->json_toks[a].size, req->json_toks[a].type, t);
    // }
}

void
proxy_add_headers(const struct vrt_ctx *ctx, struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    AN(req->busy);
    AZ(req->ctx);

    if (req->error || req->json_toks_len <= 0)
        return;

    unsigned short idx = 0;
    unsigned type = 0;
    req->ctx = ctx;

    PROXY_LOG(ctx, "start%s", "");

    process_json(req, &idx, &type, 0);

    PROXY_LOG(ctx, "end%s", "");

    req->ctx = NULL;
}

static size_t
curl_recv(void *ptr, size_t size, size_t nmemb, void *ud)
{
    struct proxy_request *req;

    CAST_OBJ_NOTNULL(req, ud, PROXY_REQUEST_MAGIC);

    VSB_bcat(req->body, ptr, size * nmemb);
    return (size * nmemb);
}

static short
process_json(struct proxy_request *req, unsigned short *idx,
             unsigned *type, unsigned short lvl)
{
    assert(req->json_toks_len > 0);
    assert(*idx < req->json_toks_len);

    char *json = VSB_data(req->body);
    AN(json);

    const struct vrt_ctx *ctx = req->ctx;
    jsmntok_t tok = req->json_toks[*idx];

    if (lvl == 0) { /* {lvl0} */
        if (req->json_toks_len > 1 && tok.type != JSMN_OBJECT)
            PROXY_REQ_ERROR_INT(req, "(%x) json error: root not object %i", req->id, req->json_toks_len);
    }
    else if (lvl == 1) { /* {'recv':[lvl1]} */
        if (*type && tok.type != JSMN_ARRAY)
            PROXY_REQ_ERROR_INT(req, "(%x) json error: lvl 1 not array", req->id);
    }
    else if (lvl == 2) { /* {'recv':["lvl2"]} */
        if (*type && tok.type != JSMN_STRING)
            PROXY_REQ_ERROR_INT(req,
                "(%x) json error: header not in \"name: value\" format", req->id);
    }
    else if (lvl >= 3 && *type) {
        // TODO: test lvl 3+. should ignore
        return 0;
    }

    //PROXY_DEBUG(ctx, "--------%s", "");
    //PROXY_DEBUG(ctx, "idx=%i type=%i size=%i lvl=%i", *idx, *type, tok.size, lvl);

    if (tok.type == JSMN_OBJECT || tok.type == JSMN_ARRAY) {
        lvl++;

        for (int i = 0; i < tok.size; i++) {
            (*idx)++;
            //PROXY_DEBUG(ctx, "LOOP: idx=%i i=%i lvl=%i", *idx, i, lvl);
            short res = process_json(req, idx, type, lvl);
            if (res == -1)
                return res;
        }

        *type = 0;
    }
    else {
        size_t len = (size_t)(tok.end - tok.start);
        char *s = json + tok.start;

        if (lvl == 1) {
            if (*type == 0) {
                if (strncmp(s, "vcl_recv", len) == 0)
                    *type = VCL_MET_RECV;
                else if (strncmp(s, "vcl_backend_fetch", len) == 0)
                    *type = VCL_MET_BACKEND_FETCH;
                else if (strncmp(s, "vcl_backend_response", len) == 0)
                    *type = VCL_MET_BACKEND_RESPONSE;
                else if (strncmp(s, "vcl_deliver", len) == 0)
                    *type = VCL_MET_DELIVER;
            }
        }
        else if (lvl == 2) {
            struct http *hp = NULL;

            if (*type == VCL_MET_RECV &&
                ctx->method == VCL_MET_RECV) {
                //PROXY_DEBUG(ctx, "SHREQ: idx=%i type=%i", *idx, *type);
                hp = ctx->http_req;
            }
            else if (*type == VCL_MET_BACKEND_FETCH &&
                     ctx->method == VCL_MET_BACKEND_FETCH) {
                //PROXY_DEBUG(ctx, "SHBF: idx=%i type=%i", *idx, *type);
                hp = ctx->http_bereq;
            }
            else if (*type == VCL_MET_BACKEND_RESPONSE &&
                     ctx->method == VCL_MET_BACKEND_RESPONSE) {
                //PROXY_DEBUG(ctx, "SHBR: idx=%i type=%i", *idx, *type);
                hp = ctx->http_beresp;
            }
            else if (*type == VCL_MET_DELIVER &&
                     ctx->method == VCL_MET_DELIVER) {
                //PROXY_DEBUG(ctx, "SHRES: idx=%i type=%i", *idx, *type);
                hp = ctx->http_resp;
            }
            else
                return 0;

            if (memchr(s, ':', len)) {
                /* Unescape string and copy to header */
                char *sp;
                char *hdr = WS_Alloc(ctx->ws, (unsigned)len + 1);
                char *hdrp = hdr;

                for (sp = s; sp < (s + len); sp++) {
                    if (*sp == '\\' && (sp + 1 < s + len)) {
                        switch (*(sp +1)) {
                            case '\"': case '/': case '\\':
                                sp++;

                            default:
                                break;
                        }
                    }

                    memcpy(hdrp++, sp, 1L);
                }

                *hdrp = '\0';

                http_SetHeader(hp, hdr);
            }

        }
    }

    return 0;
}

static void
set_header_id(const struct vrt_ctx *ctx, const struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

    char *hdr = WS_Printf(ctx->ws, "%s: %i", PROXY_HEADER, req->id);
    AN(hdr);

    http_SetHeader(ctx->http_req, hdr);
}

static unsigned
get_header_id(const struct vrt_ctx *ctx)
{
    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

    char *ids = NULL;

    size_t len = strlen(PROXY_HEADER);
    char hdr[len + 3]; /* 3 = size, colon, newline */
    sprintf(hdr, "%c%s:", (char)(len + 1), PROXY_HEADER);

    const struct http *hp = NULL;

    switch (ctx->method) {
        case VCL_MET_RECV:
        case VCL_MET_DELIVER:
        case VCL_MET_PIPE:
        case VCL_MET_SYNTH:
            hp = ctx->http_req;
            break;

        case VCL_MET_BACKEND_FETCH:
        case VCL_MET_BACKEND_RESPONSE:
        case VCL_MET_BACKEND_ERROR:
            hp = ctx->http_bereq;
            break;

        default:
            assert(0);
    }

    if (http_GetHdr(hp, hdr, &ids) == 0)
        return 0;

    errno = 0;
    unsigned id = (unsigned)strtoul(ids, NULL, 0);
    if (errno != 0 || id < 1)
        return 0;

    return id;
}

static int
is_header(const txt *hh, const char *hdr)
{
    unsigned l;

  	Tcheck(*hh);
  	AN(hdr);
  	l = hdr[0];
  	assert(l == strlen(hdr + 1));
  	assert(hdr[l] == ':');
  	hdr++;
  	return (!strncasecmp(hdr, hh->b, l));
}

static void
gc_request_pool()
{
    /* 1 in 100 chance of GC */
    if ((gc_running == 1) || (drand48() > .01))
        return;

    AZ(pthread_mutex_lock(&mtx));

    short run = 0;

    if (gc_running == 0) {
        gc_running = 1;
        run = 1;
    }

    AZ(pthread_mutex_unlock(&mtx));

    if (run) {
        struct proxy_request *req = NULL;

        AZ(pthread_mutex_lock(&mtx));

        VTAILQ_FOREACH(req, &req_pool, list) {
            if (req->ts < (time(NULL) - 600)) {
                VTAILQ_REMOVE(&req_pool, req, list);

                req->id = 0;
                req->busy = 0;
                req->ctx = NULL;
                req->methods = 0;
                req->ts = 0;
                req->error = NULL;

                if (req->body)
                    VSB_delete(req->body);
                req->body = NULL;

                FREE_OBJ(req);
            }
        }

        gc_running = 0;

        AZ(pthread_mutex_unlock(&mtx));
    }
}
