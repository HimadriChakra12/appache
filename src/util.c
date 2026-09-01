/* util.c - see util.h */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>

#include "util.h"

char *xstrdup(const char *s) {
    char *r = strdup(s ? s : "");
    if (!r) { fprintf(stderr, "appache: out of memory\n"); abort(); }
    return r;
}

char *xasprintf(const char *fmt, ...) {
    va_list ap;
    char *out = NULL;
    va_start(ap, fmt);
    if (vasprintf(&out, fmt, ap) < 0) {
        fprintf(stderr, "appache: out of memory\n");
        abort();
    }
    va_end(ap);
    return out;
}

int file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

int is_executable(const char *path) {
    return path && access(path, X_OK) == 0;
}

int str_ends_with_ci(const char *s, const char *suffix) {
    size_t ls, lf;
    if (!s || !suffix) return 0;
    ls = strlen(s);
    lf = strlen(suffix);
    if (lf > ls) return 0;
    return strcasecmp(s + (ls - lf), suffix) == 0;
}

const char *strip_leading_v(const char *s) {
    if (s && (s[0] == 'v' || s[0] == 'V') && isdigit((unsigned char)s[1]))
        return s + 1;
    return s;
}

int mkdir_p(const char *path) {
    char *copy, *p;
    int rc = 0;

    if (!path || !*path) return -1;
    copy = xstrdup(path);

    for (p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(copy, 0755) != 0 && errno != EEXIST) { rc = -1; goto done; }
            *p = '/';
        }
    }
    if (mkdir(copy, 0755) != 0 && errno != EEXIST) rc = -1;

done:
    free(copy);
    return rc;
}

int rm_rf(const char *path) {
    struct stat st;
    DIR *d;
    struct dirent *ent;

    if (!path || !*path) return -1;
    if (lstat(path, &st) != 0) return 0; /* nothing to do */

    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }

    d = opendir(path);
    if (!d) return -1;

    while ((ent = readdir(d)) != NULL) {
        char *child;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        child = xasprintf("%s/%s", path, ent->d_name);
        rm_rf(child);
        free(child);
    }
    closedir(d);
    return rmdir(path);
}

char *slugify(const char *name) {
    size_t i, j = 0, len;
    char *out;
    int last_dash = 0;

    if (!name) name = "app";
    len = strlen(name);
    out = malloc(len + 1);
    if (!out) { fprintf(stderr, "appache: out of memory\n"); abort(); }

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c)) {
            out[j++] = (char)tolower(c);
            last_dash = 0;
        } else if (!last_dash && j > 0) {
            out[j++] = '-';
            last_dash = 1;
        }
    }
    while (j > 0 && out[j - 1] == '-') j--;
    out[j] = '\0';

    if (j == 0) {
        free(out);
        return xstrdup("app");
    }
    return out;
}

char *unique_slug(const char *dir, const char *base, const char *suffix) {
    char *candidate = xasprintf("%s/%s%s", dir, base, suffix);
    int n = 2;
    if (!file_exists(candidate)) {
        free(candidate);
        return xstrdup(base);
    }
    free(candidate);
    for (;;) {
        char *try_base = xasprintf("%s-%d", base, n);
        candidate = xasprintf("%s/%s%s", dir, try_base, suffix);
        if (!file_exists(candidate)) {
            free(candidate);
            return try_base;
        }
        free(candidate);
        free(try_base);
        n++;
    }
}

static char *xdg_data_home(void) {
    const char *env = getenv("XDG_DATA_HOME");
    if (env && *env) return xstrdup(env);
    {
        const char *home = getenv("HOME");
        if (!home || !*home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        return xasprintf("%s/.local/share", home);
    }
}

static char *xdg_config_home(void) {
    const char *env = getenv("XDG_CONFIG_HOME");
    if (env && *env) return xstrdup(env);
    {
        const char *home = getenv("HOME");
        if (!home || !*home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        return xasprintf("%s/.local/share", home);
    }
}

char *appache_data_dir(void) {
    char *base = xdg_data_home();
    char *r = xasprintf("%s/appache", base);
    free(base);
    return r;
}

char *appache_apps_dir(void) {
    char *d = appache_data_dir();
    char *r = xasprintf("%s/appimages", d);
    free(d);
    return r;
}

char *appache_icons_dir(void) {
    char *d = appache_data_dir();
    char *r = xasprintf("%s/icons", d);
    free(d);
    return r;
}

char *appache_store_path(void) {
    char *d = xdg_config_home();
    char *r = xasprintf("%s/store.json", d);
    free(d);
    return r;
}

char *appache_desktop_dir(void) {
    char *base = xdg_data_home();
    char *r = xasprintf("%s/applications", base);
    free(base);
    return r;
}

char *appache_tmp_dir(void) {
    char *d = appache_data_dir();
    char *r = xasprintf("%s/tmp/appache-XXXXXX", d);
    free(d);
    {
        char *tmpbase = appache_data_dir();
        char *tmpparent = xasprintf("%s/tmp", tmpbase);
        mkdir_p(tmpparent);
        free(tmpparent);
        free(tmpbase);
    }
    if (!mkdtemp(r)) {
        free(r);
        return NULL;
    }
    return r;
}

int ensure_dirs(void) {
    int rc = 0;
    char *d;
    d = appache_data_dir();    rc |= mkdir_p(d); free(d);
    d = appache_apps_dir();    rc |= mkdir_p(d); free(d);
    d = appache_icons_dir();   rc |= mkdir_p(d); free(d);
    d = appache_desktop_dir(); rc |= mkdir_p(d); free(d);
    return rc;
}

int run_argv(char *const argv[], const char *cwd, int quiet) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        if (cwd && chdir(cwd) != 0) _exit(127);
        if (quiet) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}
