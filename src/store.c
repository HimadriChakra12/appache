/* store.c - see store.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"
#include "util.h"
#include "../vendor/cJSON.h"

static const char *src_type_str(SourceType t) {
    switch (t) {
        case SRC_GITHUB: return "github";
        case SRC_DIRECT:  return "direct";
        default:           return "none";
    }
}

static SourceType src_type_parse(const char *s) {
    if (!s) return SRC_NONE;
    if (strcmp(s, "github") == 0) return SRC_GITHUB;
    if (strcmp(s, "direct") == 0) return SRC_DIRECT;
    return SRC_NONE;
}

static const char *jstr(cJSON *obj, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) return v->valuestring;
    return "";
}

void entry_free(AppEntry *e) {
    if (!e) return;
    free(e->id);
    free(e->name);
    free(e->version);
    free(e->appimage_path);
    free(e->desktop_path);
    free(e->icon_path);
    free(e->source_repo);
    free(e->source_url);
    free(e->latest_version);
    free(e->latest_asset_url);
    free(e);
}

void list_free(AppList *list) {
    AppEntry *e;
    if (!list) return;
    e = list->head;
    while (e) {
        AppEntry *next = e->next;
        entry_free(e);
        e = next;
    }
    free(list);
}

void list_append(AppList *list, AppEntry *entry) {
    entry->next = NULL;
    if (!list->head) {
        list->head = entry;
    } else {
        AppEntry *e = list->head;
        while (e->next) e = e->next;
        e->next = entry;
    }
    list->count++;
}

AppEntry *list_find(AppList *list, const char *id) {
    AppEntry *e = list->head;
    while (e) {
        if (strcmp(e->id, id) == 0) return e;
        e = e->next;
    }
    return NULL;
}

int list_remove(AppList *list, const char *id) {
    AppEntry *e = list->head, *prev = NULL;
    while (e) {
        if (strcmp(e->id, id) == 0) {
            if (prev) prev->next = e->next;
            else list->head = e->next;
            entry_free(e);
            list->count--;
            return 1;
        }
        prev = e;
        e = e->next;
    }
    return 0;
}

AppList *store_load(void) {
    AppList *list = calloc(1, sizeof(*list));
    char *path = appache_store_path();
    FILE *f;
    char *buf;
    long len;
    cJSON *root, *apps, *item;

    f = fopen(path, "rb");
    free(path);
    if (!f) return list; /* first run: empty list is fine */

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return list; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return list; }
    buf[len] = '\0';
    fclose(f);

    root = cJSON_Parse(buf);
    free(buf);
    if (!root) return list;

    apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
    cJSON_ArrayForEach(item, apps) {
        AppEntry *e = calloc(1, sizeof(*e));
        e->id             = xstrdup(jstr(item, "id"));
        e->name           = xstrdup(jstr(item, "name"));
        e->version        = xstrdup(jstr(item, "version"));
        e->appimage_path  = xstrdup(jstr(item, "appimage_path"));
        e->desktop_path   = xstrdup(jstr(item, "desktop_path"));
        e->icon_path      = xstrdup(jstr(item, "icon_path"));
        e->source_type    = src_type_parse(jstr(item, "source_type"));
        e->source_repo    = xstrdup(jstr(item, "source_repo"));
        e->source_url     = xstrdup(jstr(item, "source_url"));
        if (e->id[0] == '\0') { entry_free(e); continue; }
        list_append(list, e);
    }

    cJSON_Delete(root);
    return list;
}

int store_save(const AppList *list) {
    cJSON *root = cJSON_CreateObject();
    cJSON *apps = cJSON_CreateArray();
    AppEntry *e;
    char *path;
    char *text;
    FILE *f;
    int rc = 0;

    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON_AddItemToObject(root, "apps", apps);

    for (e = list->head; e; e = e->next) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", e->id);
        cJSON_AddStringToObject(item, "name", e->name);
        cJSON_AddStringToObject(item, "version", e->version ? e->version : "");
        cJSON_AddStringToObject(item, "appimage_path", e->appimage_path);
        cJSON_AddStringToObject(item, "desktop_path", e->desktop_path);
        cJSON_AddStringToObject(item, "icon_path", e->icon_path ? e->icon_path : "");
        cJSON_AddStringToObject(item, "source_type", src_type_str(e->source_type));
        cJSON_AddStringToObject(item, "source_repo", e->source_repo ? e->source_repo : "");
        cJSON_AddStringToObject(item, "source_url", e->source_url ? e->source_url : "");
        cJSON_AddItemToArray(apps, item);
    }

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return -1;

    path = appache_store_path();
    ensure_dirs();
    f = fopen(path, "w");
    if (!f) { rc = -1; goto done; }
    fputs(text, f);
    fclose(f);

done:
    free(path);
    free(text);
    return rc;
}
