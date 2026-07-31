/* desktop.c - see desktop.h */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#include "desktop.h"
#include "util.h"

static char *trim_dup(const char *s) {
    const char *start = s, *end;
    while (*start && isspace((unsigned char)*start)) start++;
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) end--;
    return strndup(start, (size_t)(end - start));
}

DesktopInfo *parse_desktop_file(const char *path) {
    FILE *f;
    char line[4096];
    int in_main_group = 0;
    DesktopInfo *info;

    f = fopen(path, "r");
    if (!f) return NULL;

    info = calloc(1, sizeof(*info));
    if (!info) { fclose(f); return NULL; }

    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] == '[') {
            in_main_group = (strncmp(line, "[Desktop Entry]", 15) == 0);
            continue;
        }
        if (!in_main_group) continue;
        if (line[0] == '#' || line[0] == '\0') continue;

        {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            {
                char *key = trim_dup(line);
                char *val = trim_dup(eq + 1);

                if (strcmp(key, "Name") == 0 && !info->name) {
                    info->name = val; val = NULL;
                } else if (strcmp(key, "Icon") == 0 && !info->icon) {
                    info->icon = val; val = NULL;
                } else if (strcmp(key, "Exec") == 0 && !info->exec) {
                    info->exec = val; val = NULL;
                } else if (strcmp(key, "Categories") == 0 && !info->categories) {
                    info->categories = val; val = NULL;
                } else if (strcmp(key, "Comment") == 0 && !info->comment) {
                    info->comment = val; val = NULL;
                } else if (strcmp(key, "X-AppImage-Version") == 0 && !info->version) {
                    info->version = val; val = NULL;
                }
                free(key);
                free(val);
            }
        }
    }

    fclose(f);
    return info;
}

void desktop_info_free(DesktopInfo *info) {
    if (!info) return;
    free(info->name);
    free(info->icon);
    free(info->exec);
    free(info->categories);
    free(info->comment);
    free(info->version);
    free(info);
}

char *find_desktop_file(const char *root) {
    DIR *d = opendir(root);
    struct dirent *ent;
    char *result = NULL;

    if (!d) return NULL;
    while ((ent = readdir(d)) != NULL) {
        if (str_ends_with_ci(ent->d_name, ".desktop")) {
            result = xasprintf("%s/%s", root, ent->d_name);
            break;
        }
    }
    closedir(d);
    return result;
}

static char *try_exts(const char *dir, const char *base) {
    static const char *exts[] = { ".png", ".svg", ".xpm", ".jpg", ".jpeg", NULL };
    int i;
    for (i = 0; exts[i]; i++) {
        char *p = xasprintf("%s/%s%s", dir, base, exts[i]);
        if (file_exists(p)) return p;
        free(p);
    }
    /* the icon key sometimes already includes an extension */
    {
        char *p = xasprintf("%s/%s", dir, base);
        if (file_exists(p)) return p;
        free(p);
    }
    return NULL;
}

static char *scan_hicolor(const char *root, const char *base) {
    /* usr/share/icons/hicolor/<size>/apps/<base>.<ext>, biggest size first */
    static const char *sizes[] = {
        "512x512", "256x256", "128x128", "96x96", "64x64", "48x48", "32x32",
        "scalable", NULL
    };
    int i;
    for (i = 0; sizes[i]; i++) {
        char *dir = xasprintf("%s/usr/share/icons/hicolor/%s/apps", root, sizes[i]);
        char *found = try_exts(dir, base);
        free(dir);
        if (found) return found;
    }
    return NULL;
}

static char *any_toplevel_image(const char *root) {
    DIR *d = opendir(root);
    struct dirent *ent;
    char *result = NULL;

    if (!d) return NULL;
    while ((ent = readdir(d)) != NULL) {
        if (str_ends_with_ci(ent->d_name, ".png") ||
            str_ends_with_ci(ent->d_name, ".svg") ||
            str_ends_with_ci(ent->d_name, ".diricon")) {
            result = xasprintf("%s/%s", root, ent->d_name);
            break;
        }
    }
    closedir(d);
    return result;
}

char *find_icon_file(const char *root, const char *icon_key) {
    char *found;

    if (icon_key && *icon_key) {
        /* icon_key may itself be an absolute path in some malformed builds */
        if (icon_key[0] == '/' && file_exists(icon_key))
            return xstrdup(icon_key);

        found = try_exts(root, icon_key);
        if (found) return found;

        found = scan_hicolor(root, icon_key);
        if (found) return found;
    }

    /* AppImages conventionally ship a top-level .DirIcon or <name>.png */
    found = try_exts(root, ".DirIcon");
    if (found) return found;

    return any_toplevel_image(root);
}

int write_desktop_entry(const char *dest_path, const char *name,
                         const char *appimage_path, const char *icon_path,
                         const char *categories, const char *version) {
    FILE *f = fopen(dest_path, "w");
    if (!f) return -1;

    fprintf(f, "[Desktop Entry]\n");
    fprintf(f, "Type=Application\n");
    fprintf(f, "Name=%s\n", name ? name : "AppImage");
    fprintf(f, "Exec=\"%s\" %%U\n", appimage_path);
    if (icon_path && *icon_path)
        fprintf(f, "Icon=%s\n", icon_path);
    fprintf(f, "Terminal=false\n");
    if (categories && *categories)
        fprintf(f, "Categories=%s\n", categories);
    else
        fprintf(f, "Categories=Utility;\n");
    if (version && *version)
        fprintf(f, "X-AppImage-Version=%s\n", version);
    fprintf(f, "X-AppImage-Managed-By=appache\n");
    fprintf(f, "Comment=Installed with appache\n");

    fclose(f);
    chmod(dest_path, 0644);
    return 0;
}

void refresh_desktop_db(void) {
    char *desktop_dir = appache_desktop_dir();
    {
        char *argv[] = { "update-desktop-database", desktop_dir, NULL };
        run_argv(argv, NULL, 1);
    }
    {
        char *argv[] = { "gtk-update-icon-cache", NULL };
        run_argv(argv, NULL, 1);
    }
    free(desktop_dir);
}
