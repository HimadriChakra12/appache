/* net.c - see net.h */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

#include "net.h"

#define APPACHE_USER_AGENT "appache-appimage-manager/1.0"
#define APPACHE_TIMEOUT_SECONDS 30L

struct mem_buf {
    char *data;
    size_t len;
};

static size_t mem_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct mem_buf *m = userdata;
    size_t add = size * nmemb;
    char *grown = realloc(m->data, m->len + add + 1);
    if (!grown) return 0;
    m->data = grown;
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

int net_get(const char *url, char **out_data, size_t *out_len) {
    CURL *c;
    CURLcode res;
    long status = 0;
    struct mem_buf buf = { NULL, 0 };

    c = curl_easy_init();
    if (!c) return -1;

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_USERAGENT, APPACHE_USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, APPACHE_TIMEOUT_SECONDS);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);

    res = curl_easy_perform(c);
    if (res == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);

    if (res != CURLE_OK || status < 200 || status >= 300) {
        free(buf.data);
        return -1;
    }

    *out_data = buf.data ? buf.data : strdup("");
    *out_len = buf.len;
    return 0;
}

struct dl_ctx {
    FILE *f;
    net_progress_fn progress;
    void *user_data;
};

static size_t file_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct dl_ctx *ctx = userdata;
    return fwrite(ptr, size, nmemb, ctx->f);
}

static int xferinfo_cb(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow) {
    struct dl_ctx *ctx = userdata;
    (void)ultotal; (void)ulnow;
    if (ctx->progress) {
        if (dltotal > 0)
            ctx->progress((double)dlnow / (double)dltotal, ctx->user_data);
        else
            ctx->progress(-1.0, ctx->user_data);
    }
    return 0;
}

int net_download(const char *url, const char *dest_path,
                  net_progress_fn progress, void *user_data) {
    CURL *c;
    CURLcode res;
    long status = 0;
    struct dl_ctx ctx;

    ctx.f = fopen(dest_path, "wb");
    if (!ctx.f) return -1;
    ctx.progress = progress;
    ctx.user_data = user_data;

    c = curl_easy_init();
    if (!c) { fclose(ctx.f); return -1; }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(c, CURLOPT_USERAGENT, APPACHE_USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);

    res = curl_easy_perform(c);
    if (res == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    fclose(ctx.f);

    if (res != CURLE_OK || status < 200 || status >= 300) {
        unlink(dest_path);
        return -1;
    }
    return 0;
}
