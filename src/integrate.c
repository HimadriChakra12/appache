/* integrate.c - see integrate.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gio/gio.h>

#include "integrate.h"
#include "desktop.h"
#include "util.h"

/* copy_file: simple buffered copy, preserving the executable bit. */
static int copy_file(const char *src, const char *dst) {
    FILE *in, *out;
    char buf[65536];
    size_t n;
    struct stat st;

    in = fopen(src, "rb");
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return -1; }
    }
    fclose(in);
    fclose(out);

    if (stat(src, &st) == 0) chmod(dst, st.st_mode | 0111);
    return 0;
}

/* extract_appimage: run `appimage_path --appimage-extract` inside a fresh
 * temp dir, returning the path to the resulting squashfs-root/ (caller
 * frees), or NULL on failure. `tmp_out` receives the temp dir itself so the
 * caller can rm_rf() it afterwards. */
static char *extract_appimage(const char *appimage_path, char **tmp_out) {
    char *tmp = appache_tmp_dir();
    char *abspath;
    char *root;

    if (!tmp) return NULL;

    if (appimage_path[0] == '/') {
        abspath = xstrdup(appimage_path);
    } else {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) { rm_rf(tmp); free(tmp); return NULL; }
        abspath = xasprintf("%s/%s", cwd, appimage_path);
    }

    {
        char *argv[] = { abspath, "--appimage-extract", NULL };
        int rc = run_argv(argv, tmp, 1);
        free(abspath);
        if (rc != 0) { rm_rf(tmp); free(tmp); return NULL; }
    }

    root = xasprintf("%s/squashfs-root", tmp);
    if (!file_exists(root)) {
        free(root);
        rm_rf(tmp);
        free(tmp);
        return NULL;
    }

    *tmp_out = tmp;
    return root;
}

AppEntry *integrate_appimage(const char *src_path, char **err_out) {
    char *tmp = NULL, *root = NULL;
    char *desktop_src = NULL;
    DesktopInfo *info = NULL;
    char *base_slug = NULL, *slug = NULL;
    char *apps_dir = NULL, *icons_dir = NULL, *desktop_dir = NULL;
    char *managed_appimage = NULL, *managed_icon = NULL, *managed_desktop = NULL;
    char *icon_src = NULL;
    AppEntry *entry = NULL;
    const char *display_name;

    if (err_out) *err_out = NULL;

    if (!file_exists(src_path)) {
        if (err_out) *err_out = xasprintf("File not found: %s", src_path);
        return NULL;
    }
    chmod(src_path, 0755);

    if (ensure_dirs() != 0) {
        if (err_out) *err_out = xstrdup("Could not create ~/.local/share/appache");
        return NULL;
    }

    root = extract_appimage(src_path, &tmp);
    if (!root) {
        if (err_out) *err_out = xasprintf(
            "Could not extract %s (is it a valid, executable AppImage?)", src_path);
        return NULL;
    }

    desktop_src = find_desktop_file(root);
    if (desktop_src) info = parse_desktop_file(desktop_src);

    /* fall back to the filename if the AppImage has no usable metadata */
    display_name = (info && info->name && *info->name) ? info->name : NULL;
    if (!display_name) {
        const char *base = strrchr(src_path, '/');
        base = base ? base + 1 : src_path;
        display_name = base;
    }

    apps_dir = appache_apps_dir();
    icons_dir = appache_icons_dir();
    desktop_dir = appache_desktop_dir();

    base_slug = slugify(display_name);
    slug = unique_slug(apps_dir, base_slug, ".AppImage");
    free(base_slug);

    managed_appimage = xasprintf("%s/%s.AppImage", apps_dir, slug);
    if (copy_file(src_path, managed_appimage) != 0) {
        if (err_out) *err_out = xstrdup("Failed to copy AppImage into place");
        goto fail;
    }

    icon_src = find_icon_file(root, info ? info->icon : NULL);
    if (icon_src) {
        const char *dot = strrchr(icon_src, '.');
        const char *ext = (dot && strlen(dot) <= 6) ? dot : ".png";
        managed_icon = xasprintf("%s/%s%s", icons_dir, slug, ext);
        if (copy_file(icon_src, managed_icon) != 0) {
            free(managed_icon);
            managed_icon = NULL;
        }
    }

    managed_desktop = xasprintf("%s/appache-%s.desktop", desktop_dir, slug);
    write_desktop_entry(managed_desktop, display_name, managed_appimage,
                         managed_icon, info ? info->categories : NULL,
                         info ? info->version : NULL);

    entry = calloc(1, sizeof(*entry));
    entry->id = xstrdup(slug);
    entry->name = xstrdup(display_name);
    entry->version = xstrdup(info && info->version ? info->version : "");
    entry->appimage_path = managed_appimage;
    entry->desktop_path = managed_desktop;
    entry->icon_path = managed_icon ? xstrdup(managed_icon) : xstrdup("");
    entry->source_type = SRC_NONE;

    managed_appimage = NULL; /* ownership moved into entry */
    managed_desktop = NULL;

    refresh_desktop_db();

    /* the caller's source copy (e.g. a temp download) is no longer needed */
    unlink(src_path);

fail:
    free(slug);
    free(apps_dir);
    free(icons_dir);
    free(desktop_dir);
    free(managed_appimage);
    free(managed_icon);
    free(managed_desktop);
    free(icon_src);
    free(desktop_src);
    desktop_info_free(info);
    if (tmp) { rm_rf(tmp); free(tmp); }
    if (root) free(root);
    return entry;
}

int reintegrate_appimage(AppEntry *entry, char **err_out) {
    char *tmp = NULL, *root = NULL;
    char *desktop_src = NULL;
    DesktopInfo *info = NULL;
    char *icon_src = NULL;
    char *icons_dir = NULL;
    char *new_icon = NULL;

    if (err_out) *err_out = NULL;
    chmod(entry->appimage_path, 0755);

    root = extract_appimage(entry->appimage_path, &tmp);
    if (!root) {
        if (err_out) *err_out = xstrdup("Could not extract the updated AppImage");
        return -1;
    }

    desktop_src = find_desktop_file(root);
    if (desktop_src) info = parse_desktop_file(desktop_src);

    if (info && info->name && *info->name) {
        free(entry->name);
        entry->name = xstrdup(info->name);
    }
    if (info && info->version && *info->version) {
        free(entry->version);
        entry->version = xstrdup(info->version);
    }

    icons_dir = appache_icons_dir();
    icon_src = find_icon_file(root, info ? info->icon : NULL);
    if (icon_src) {
        const char *dot = strrchr(icon_src, '.');
        const char *ext = (dot && strlen(dot) <= 6) ? dot : ".png";
        new_icon = xasprintf("%s/%s%s", icons_dir, entry->id, ext);
        if (copy_file(icon_src, new_icon) == 0) {
            free(entry->icon_path);
            entry->icon_path = new_icon;
            new_icon = NULL;
        }
    }

    write_desktop_entry(entry->desktop_path, entry->name, entry->appimage_path,
                         entry->icon_path, info ? info->categories : NULL,
                         entry->version);
    refresh_desktop_db();

    free(new_icon);
    free(icon_src);
    free(icons_dir);
    free(desktop_src);
    desktop_info_free(info);
    if (tmp) { rm_rf(tmp); free(tmp); }
    if (root) free(root);
    return 0;
}

static void trash_path(const char *path) {
    GFile *f;
    if (!path || !*path || !file_exists(path)) return;
    f = g_file_new_for_path(path);
    g_file_trash(f, NULL, NULL); /* best-effort; falls back silently */
    g_object_unref(f);
}

int remove_appimage(AppEntry *entry) {
    trash_path(entry->appimage_path);
    trash_path(entry->desktop_path);
    trash_path(entry->icon_path);
    refresh_desktop_db();
    return 0;
}
