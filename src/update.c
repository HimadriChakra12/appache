/* update.c - see update.h */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <sys/stat.h>

#include "update.h"
#include "integrate.h"
#include "net.h"
#include "util.h"
#include "../vendor/cJSON.h"

static const char *machine_arch(void) {
    static char arch[64] = {0};
    struct utsname u;
    if (arch[0]) return arch;
    if (uname(&u) == 0) {
        strncpy(arch, u.machine, sizeof(arch) - 1);
        arch[sizeof(arch) - 1] = '\0';
    } else {
        snprintf(arch, sizeof(arch), "x86_64");
    }
    return arch;
}

/* arch_matches: loose match between a release asset filename and this
 * machine's architecture, accepting common aliases. */
static int arch_matches(const char *filename, const char *arch) {
    if (strcasestr(filename, arch)) return 1;
    if (strcmp(arch, "x86_64") == 0 &&
        (strcasestr(filename, "amd64") || strcasestr(filename, "x64")))
        return 1;
    if (strcmp(arch, "aarch64") == 0 && strcasestr(filename, "arm64"))
        return 1;
    return 0;
}

static int check_github_update(AppEntry *e) {
    char *url;
    char *data = NULL;
    size_t len = 0;
    cJSON *root, *tag, *assets, *asset;
    const char *arch = machine_arch();
    char *best_url = NULL, *best_name = NULL;
    char *fallback_url = NULL;
    int rc = -1;

    if (!e->source_repo || !*e->source_repo) return -1;

    url = xasprintf("https://api.github.com/repos/%s/releases/latest", e->source_repo);
    if (net_get(url, &data, &len) != 0) { free(url); return -1; }
    free(url);

    root = cJSON_ParseWithLength(data, len);
    free(data);
    if (!root) return -1;

    tag = cJSON_GetObjectItemCaseSensitive(root, "tag_name");
    assets = cJSON_GetObjectItemCaseSensitive(root, "assets");

    if (!cJSON_IsString(tag)) { cJSON_Delete(root); return -1; }

    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        cJSON *dlurl = cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url");
        if (!cJSON_IsString(name) || !cJSON_IsString(dlurl)) continue;
        if (!str_ends_with_ci(name->valuestring, ".appimage")) continue;

        if (!fallback_url) fallback_url = xstrdup(dlurl->valuestring);

        if (arch_matches(name->valuestring, arch)) {
            free(best_url);
            free(best_name);
            best_url = xstrdup(dlurl->valuestring);
            best_name = xstrdup(name->valuestring);
        }
    }

    if (!best_url && fallback_url) {
        best_url = fallback_url;
        fallback_url = NULL;
    }

    if (best_url) {
        free(e->latest_version);
        free(e->latest_asset_url);
        e->latest_version = xstrdup(strip_leading_v(tag->valuestring));
        e->latest_asset_url = best_url;
        best_url = NULL;

        e->update_available =
            (strcmp(strip_leading_v(e->version ? e->version : ""),
                    e->latest_version) != 0);
        rc = 0;
    }

    free(best_name);
    free(fallback_url);
    cJSON_Delete(root);
    return rc;
}

int check_update(AppEntry *entry) {
    if (entry->source_type == SRC_GITHUB) {
        return check_github_update(entry);
    }
    if (entry->source_type == SRC_DIRECT) {
        /* No reliable remote version marker for an arbitrary direct link;
         * surface it as "an update can be fetched" and let the user decide
         * when to re-pull it. */
        entry->update_available = 1;
        return 0;
    }
    entry->update_available = 0;
    return 0;
}

int apply_update(AppEntry *entry, char **err_out) {
    const char *url = NULL;
    char *tmp_dir;
    char *tmp_file;

    if (err_out) *err_out = NULL;

    if (entry->source_type == SRC_GITHUB) url = entry->latest_asset_url;
    else if (entry->source_type == SRC_DIRECT) url = entry->source_url;

    if (!url || !*url) {
        if (err_out) *err_out = xstrdup("No update source configured for this app");
        return -1;
    }

    tmp_dir = appache_tmp_dir();
    if (!tmp_dir) {
        if (err_out) *err_out = xstrdup("Could not create a temp directory");
        return -1;
    }
    tmp_file = xasprintf("%s/update.AppImage", tmp_dir);

    if (net_download(url, tmp_file, NULL, NULL) != 0) {
        if (err_out) *err_out = xasprintf("Download failed: %s", url);
        free(tmp_file);
        rm_rf(tmp_dir);
        free(tmp_dir);
        return -1;
    }
    chmod(tmp_file, 0755);

    /* replace the managed AppImage in place */
    if (rename(tmp_file, entry->appimage_path) != 0) {
        /* cross-device fallback: copy then unlink */
        FILE *in = fopen(tmp_file, "rb");
        FILE *out = in ? fopen(entry->appimage_path, "wb") : NULL;
        if (in && out) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        }
        if (in) fclose(in);
        if (out) fclose(out);
        unlink(tmp_file);
        chmod(entry->appimage_path, 0755);
    }

    rm_rf(tmp_dir);
    free(tmp_dir);
    free(tmp_file);

    if (entry->source_type == SRC_GITHUB && entry->latest_version) {
        free(entry->version);
        entry->version = xstrdup(entry->latest_version);
    }
    entry->update_available = 0;

    return reintegrate_appimage(entry, err_out);
}

AppEntry *add_from_url(const char *url, char **err_out) {
    char *tmp_dir;
    char *tmp_file;
    AppEntry *entry;

    if (err_out) *err_out = NULL;

    tmp_dir = appache_tmp_dir();
    if (!tmp_dir) {
        if (err_out) *err_out = xstrdup("Could not create a temp directory");
        return NULL;
    }
    tmp_file = xasprintf("%s/download.AppImage", tmp_dir);

    if (net_download(url, tmp_file, NULL, NULL) != 0) {
        if (err_out) *err_out = xasprintf("Download failed: %s", url);
        rm_rf(tmp_dir);
        free(tmp_dir);
        free(tmp_file);
        return NULL;
    }
    chmod(tmp_file, 0755);

    entry = integrate_appimage(tmp_file, err_out);

    rm_rf(tmp_dir);
    free(tmp_dir);
    free(tmp_file);

    if (entry) {
        entry->source_type = SRC_DIRECT;
        entry->source_url = xstrdup(url);
    }
    return entry;
}
