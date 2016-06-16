#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/errno.h>
#include <curl/curl.h>

#include "proxy.h"

static short init = 1;

/* Implementation of the static method cache_http.c::http_IsHdr() */
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

/* Implementation of the static method cache_http.c::http_findhdr() */
static unsigned
find_header(const struct http *hp, unsigned l, const char *hdr)
{
    unsigned u;

    for (u = HTTP_HDR_FIRST; u < hp->nhd; u++) {
        Tcheck(hp->hd[u]);
        if (hp->hd[u].e < hp->hd[u].b + l + 1)
            continue;
        if (hp->hd[u].b[l] != ':')
            continue;
        if (strncasecmp(hdr, hp->hd[u].b, l))
            continue;
        return (u);
    }
    return (0);
}

/* Implementation of the method cache_http.c::http_CollectHdr().
 * Unfortunately we cant use the built in one since it always separates values
 * with a comma. For the cookie header we need a colon separator. */
static void
collect_header(struct http *hp, const char *hdr, const char sep)
{
    unsigned u, l, ml, f, x, d;
    char *b = NULL, *e = NULL;

    CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
    if (WS_Overflowed(hp->ws))
        return;
    l = hdr[0];
    assert(l == strlen(hdr + 1));
    assert(hdr[l] == ':');
    f = find_header(hp, l - 1, hdr + 1);
    if (f == 0)
        return;

    for (d = u = f + 1; u < hp->nhd; u++) {
        Tcheck(hp->hd[u]);
        if (!is_header(&hp->hd[u], hdr)) {
            if (d != u) {
                hp->hd[d] = hp->hd[u];
                hp->hdf[d] = hp->hdf[u];
            }
            d++;
            continue;
        }
        if (b == NULL) {
            /* Found second header, start our collection */
            ml = WS_Reserve(hp->ws, 0);
            b = hp->ws->f;
            e = b + ml;
            x = Tlen(hp->hd[f]);
            if (b + x >= e) {
                VSLb(hp->vsl, SLT_LostHeader, "%s", hdr + 1);
                WS_Release(hp->ws, 0);
                return;
            }
            memcpy(b, hp->hd[f].b, x);
            b += x;
        }

        AN(b);
        AN(e);

        /* Append the Nth header we found */
        if (b < e)
            *b++ = sep;
        x = Tlen(hp->hd[u]) - l;
        if (b + x >= e) {
            VSLb(hp->vsl, SLT_LostHeader, "%s", hdr + 1);
            WS_Release(hp->ws, 0);
            return;
        }
        memcpy(b, hp->hd[u].b + *hdr, x);
        b += x;
    }
    if (b == NULL)
        return;
    hp->nhd = (uint16_t)d;
    AN(e);
    *b = '\0';
    hp->hd[f].b = hp->ws->f;
    hp->hd[f].e = b;
    WS_ReleaseP(hp->ws, b + 1);
}

/* Gets an available backend to curl to */
static const struct backend *
get_backend(VRT_CTX, struct worker *wrk, const struct director *dir)
{
    const struct director *be = NULL;
    const struct backend *bp = NULL;

    CHECK_OBJ_NOTNULL(dir, DIRECTOR_MAGIC);

    if (dir->resolve) {
        struct busyobj bo = {0};
        bo.magic = BUSYOBJ_MAGIC;
        be = dir->resolve(dir, wrk, &bo);
        CHECK_OBJ_ORNULL(be, DIRECTOR_MAGIC);
    }
    else
        be = dir;

    if (VALID_OBJ(be, DIRECTOR_MAGIC)) {
        CAST_OBJ_NOTNULL(bp, be->priv, BACKEND_MAGIC);
        return bp;
    }

    return NULL;
}

void
proxy_init()
{
    if (init)
        curl_global_init(CURL_GLOBAL_ALL);

    init = 0;
}

void
clear_request(struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);

    req->ctx = NULL;
    memset(req->json_toks, 0, sizeof req->json_toks);
    req->json_toks_len = 0;
    req->collect_cookies = 0;
    req->restarts = 0;
    req->error = NULL;
}

struct proxy_request *
proxy_create_request(VRT_CTX)
{
    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
    assert(ctx->method == VCL_MET_RECV);

    PROXY_DEBUG(ctx, "proxy_create_request%s", "");

    struct proxy_request *req;
    ALLOC_OBJ(req, PROXY_REQUEST_MAGIC);
    AN(req);

    clear_request(req);

    req->json = VSB_new_auto();
    CHECK_OBJ_NOTNULL(req->json, VSB_MAGIC);

    return req;
}

void
proxy_restart_request(VRT_CTX, struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
    assert(ctx->method == VCL_MET_RECV);
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);

    clear_request(req);

    req->restarts = ctx->req->restarts;

    CHECK_OBJ_NOTNULL(req->json, VSB_MAGIC);
    VSB_clear(req->json);
}

void
proxy_release_request(void *ptr)
{
    struct proxy_request *req;
    CAST_OBJ_NOTNULL(req, ptr, PROXY_REQUEST_MAGIC);

    clear_request(req);

    if (req->json)
        VSB_delete(req->json);

    FREE_OBJ(req);
}

#ifdef DEBUG
static int
curl_debug(CURL *ch, curl_infotype type, char *data, size_t size, void *ud)
{
    struct vrt_ctx *ctx;
    CAST_OBJ_NOTNULL(ctx, ud, VRT_CTX_MAGIC);

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

        PROXY_DEBUG(ctx, "curl %s: %.*s", stype, (int)len, data);
    }

    return 0;
}
#endif


static size_t
curl_recv(void *ptr, size_t size, size_t nmemb, void *ud)
{
    struct vsb *body = NULL;
    CAST_OBJ_NOTNULL(body, ud, VSB_MAGIC);

    VSB_bcat(body, ptr, size * nmemb);
    return (size * nmemb);
}

void
proxy_curl(VRT_CTX, struct proxy_request *req, const struct director *dir,
            const char *path)
{
    CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    CHECK_OBJ_NOTNULL(req->json, VSB_MAGIC);

    PROXY_DEBUG(ctx, "proxy_curl%s", "");

    AZ(req->ctx);
    req->ctx = ctx;

    const struct backend *be = get_backend(ctx, ctx->req->wrk, dir);
    CHECK_OBJ_ORNULL(be, BACKEND_MAGIC);

    if (be == NULL)
        PROXY_REQ_ERROR_VOID(req, "no backends available%s", "");

    // TODO: abstract out hostname retrieval
    char *url = WS_Printf(ctx->ws, "http://%s%s%s",
        be->ipv4_addr, (*path == '/' ? "" : "/"), path
    );

    long port = strtol(be->port, NULL, 0);

    CURL *ch = curl_easy_init();
    AN(ch);

    curl_easy_setopt(ch, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_PORT, port);
    curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, curl_recv);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, req->json);

#ifdef DEBUG
    curl_easy_setopt(ch, CURLOPT_VERBOSE, DEBUG);
    curl_easy_setopt(ch, CURLOPT_DEBUGFUNCTION, curl_debug);
    curl_easy_setopt(ch, CURLOPT_DEBUGDATA, ctx);
#endif

    if ((long)be->connect_timeout > 0)
        curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, (long)be->connect_timeout);

    if ((long)be->first_byte_timeout > 0)
        curl_easy_setopt(ch, CURLOPT_TIMEOUT, (long)be->first_byte_timeout);

    struct curl_slist *headers = NULL;
    char *wshdr = NULL;

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
            if (is_header(&hdr, H_Via) || is_header(&hdr, H_Content_Length)) {
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

    PROXY_DEBUG(ctx, "curl url:%s", be->ipv4_addr);
    CURLcode ret = curl_easy_perform(ch);

    long status;
    curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status);

    if (headers)
        curl_slist_free_all(headers);

    curl_easy_cleanup(ch);

    if (ret != 0)
        PROXY_REQ_ERROR_VOID(req, "curl err: %s", curl_easy_strerror(ret));

    // Non 200 responses should report error, but still attempt to process json
    if (status != 200) {
        req->error = WS_Printf(ctx->ws, "curl err: %lu response", status);
        PROXY_WARN(ctx, "curl err: %lu response", status);
    }

    // TODO: check header content type

    VSB_finish(req->json);
    char *json = VSB_data(req->json);
    size_t json_len = strlen(json);

    if (json_len == 0)
        PROXY_REQ_ERROR_VOID(req, "parse: no body%s", "");

    if (json_len > 0x1FFFF)
        PROXY_REQ_ERROR_VOID(req, "parse: body too big (%zu)", json_len);

    /* Save expensive json parse if doesnt open and close with {} or []  */
    char *p, fc = '\0', lc = '\0';
    for (p = json; p < (json + json_len); p++) {
        if (!isspace(*p)) {
            if (!fc)
                fc = *p;
            lc = *p;
        }
    }

    if ((fc != '{' && fc != '[') || (lc != '}' && lc != ']'))
        PROXY_REQ_ERROR_VOID(req, "parse: bad delimiters%s", "");

    jsmn_parser parser;
    jsmn_init(&parser);

    req->json_toks_len = jsmn_parse(
        &parser, json, json_len, req->json_toks, JSON_MAX_TOKENS
    );

    if (req->json_toks_len < 0)
        PROXY_REQ_ERROR_VOID(req, "parse: failed to parse json%s", "");

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

static short
process_json(struct proxy_request *req, unsigned short *idx,
             unsigned *type, unsigned short lvl)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    CHECK_OBJ_NOTNULL(req->ctx, VRT_CTX_MAGIC);
    CHECK_OBJ_NOTNULL(req->json, VSB_MAGIC);
    assert(req->json_toks_len > 0);
    assert(*idx < req->json_toks_len);

    char *json = VSB_data(req->json);

    const struct vrt_ctx *ctx = req->ctx;
    jsmntok_t tok = req->json_toks[*idx];

    if (lvl == 0) { /* {lvl0} */
        if (req->json_toks_len > 1 && tok.type != JSMN_OBJECT)
            PROXY_REQ_ERROR_INT(req, "json error: root not object %i", req->json_toks_len);
    }
    else if (lvl == 1) { /* {'recv':[lvl1]} */
        if (*type && tok.type != JSMN_ARRAY)
            PROXY_REQ_ERROR_INT(req, "json error: lvl 1 not array%s", "");
    }
    else if (lvl == 2) { /* {'recv':["lvl2"]} */
        if (*type && tok.type != JSMN_STRING)
            PROXY_REQ_ERROR_INT(req,
                "json error: header not in \"name: value\" format%s", "");
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
        const char *s = json + tok.start;

        if (lvl == 1) {
            if (*type == 0) {
                if (strncmp(s, "vcl_recv", len) == 0)
                    *type = VCL_MET_RECV;
                else if (strncmp(s, "vcl_deliver", len) == 0)
                    *type = VCL_MET_DELIVER;
            }
        }
        else if (lvl == 2) {
            struct http *hp = NULL;

            if (memchr(s, ':', len) == NULL)
                return 0;

            if (*type == VCL_MET_RECV && ctx->method == VCL_MET_RECV) {
                //PROXY_DEBUG(ctx, "SHREQ: idx=%i type=%i", *idx, *type);
                hp = ctx->http_req;
                if (strncasecmp(s, H_Cookie + 1, H_Cookie[0]) == 0)
                    req->collect_cookies = 1;
            }
            else if (*type == VCL_MET_DELIVER && ctx->method == VCL_MET_DELIVER &&
                     ctx->req->esi_level == 0) {
                //PROXY_DEBUG(ctx, "SHRES: idx=%i type=%i", *idx, *type);
                hp = ctx->http_resp;
            }
            else
                return 0;

            // Unescape string and copy to header
            const char *sp;
            char *hdr = WS_Alloc(ctx->ws, (unsigned)len + 1);
            char *hdrp = hdr;
            for (sp = s; sp < (s + len); sp++) {
                if (*sp == '\\' && (sp + 1 < s + len)) {
                    switch (*(sp + 1)) {
                        case '\"': case '/': case '\\':
                            sp++;
                    }
                }

                memcpy(hdrp++, sp, 1L);
            }
            *hdrp = '\0';

            // Handle various header names appropriately
            if (*type == VCL_MET_RECV && ctx->method == VCL_MET_RECV) {
                char nhdr[64];
                const char *cp = memchr(hdr, ':', len);
                AN(cp);
                if ((cp - hdr) < 64) {
                    memset(nhdr, 0, sizeof nhdr);
                    nhdr[0] = (char)(cp - hdr + 1);
                    strncpy((nhdr + 1), hdr, nhdr[0]);

                    if (strncasecmp(hdr, H_Cookie + 1, H_Cookie[0]) != 0)
                        http_Unset(hp, nhdr);
                }
            }

            http_SetHeader(hp, hdr);
        }
    }

    return 0;
}

void
proxy_process_request(VRT_CTX, struct proxy_request *req)
{
    CHECK_OBJ_NOTNULL(req, PROXY_REQUEST_MAGIC);
    CHECK_OBJ_NOTNULL(req->json, VSB_MAGIC);
    AZ(req->ctx);

    if (req->json_toks_len <= 0)
        return;

    unsigned short idx = 0;
    unsigned type = 0;

    PROXY_LOG(ctx, "start%s", "");

    AZ(req->ctx);
    req->ctx = ctx;
    process_json(req, &idx, &type, 0);
    req->ctx = NULL;

    if (ctx->method == VCL_MET_RECV && req->collect_cookies)
        collect_header(ctx->http_req, H_Cookie, ';');

    PROXY_LOG(ctx, "end%s", "");
}
